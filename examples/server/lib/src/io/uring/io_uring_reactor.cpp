#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <variant>

#include "async_coro/utils/always_false.h"
#if IO_URING_ENABLED

#include <async_coro/config.h>
#include <fcntl.h>
#include <liburing.h>
#include <linux/io_uring.h>
#include <server/core/error.h>
#include <server/io/uring/io_uring_reactor.h>
#include <server/utils/expected.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <span>

namespace server::io {

io_uring_reactor::io_uring_reactor() noexcept = default;

expected<io_uring_reactor, core::error> io_uring_reactor::create(size_t ring_size) noexcept {
  io_uring_reactor reactor;
  reactor._ring_size = ring_size;
  reactor._local_ring = std::make_unique<request_variant[]>(ring_size);  // NOLINT(*-c-arrays): io_uring requires contiguous heap allocation managed by unique_ptr
  reactor._free_indices.reserve(ring_size);
  reactor._events_to_push.reserve(ring_size);

  for (size_t i = 0; i < ring_size; i++) {
    reactor._free_indices.push_back(i);
  }

  struct io_uring_params params{};

  if (io_uring_queue_init_params(ring_size, &reactor._ring, &params) != 0) {
    return expected<io_uring_reactor, core::error>{unexpect, core::error_type::io_uring_init_failed, errno};
  }
  return std::move(reactor);
}

io_uring_reactor::io_uring_reactor(io_uring_reactor&& other) noexcept
    : _ring(other._ring),
      _ring_initialized(other._ring_initialized),
      _ring_size(other._ring_size),
      _local_ring(std::move(other._local_ring)),
      _free_indices(std::move(other._free_indices)) {
  other._ring = {};
  other._ring_initialized = false;
}

io_uring_reactor& io_uring_reactor::operator=(io_uring_reactor&& other) noexcept {
  if (this != &other) {
    if (_ring_initialized) {
      io_uring_queue_exit(&_ring);
    }

    _ring = other._ring;
    _ring_initialized = other._ring_initialized;
    _ring_size = other._ring_size;
    _local_ring = std::move(other._local_ring);
    _free_indices = std::move(other._free_indices);
    _events_to_push = std::move(other._events_to_push);

    other._ring = {};
    other._ring_initialized = false;
  }
  return *this;
}

io_uring_reactor::~io_uring_reactor() noexcept {
  if (_ring_initialized) {
    io_uring_queue_exit(&_ring);
  }
}

void io_uring_reactor::process_loop(std::chrono::nanoseconds max_wait) {  // NOLINT(readability-function-cognitive-complexity): complex but well-structured 4-phase io_uring processing loop
  // Phase 1: Drain atomic_queue into local ring buffer
  while (!_free_indices.empty()) {
    request_variant entry;
    if (!_requests.try_pop(entry)) {
      break;
    }
    const auto index = _free_indices.back();
    _free_indices.pop_back();

    _local_ring[index] = std::move(entry);

    _events_to_push.push_back(index);
  }

  int events_to_submit = 0;

  // Phase 2: Push events from previous loop or submitted in this loop
  while (!_events_to_push.empty()) {
    const auto index = _events_to_push.back();

    io_uring_sqe* sqe_ptr = io_uring_get_sqe(&_ring);
    if (sqe_ptr == nullptr) {
      // Ring full — leave remaining entries in atomic_queue for next update call
      break;
    }

    // Submit SQE
    auto& entry_ref = _local_ring[index];

    std::visit([sqe_ptr](auto& operation) {
      using op_type = std::decay_t<decltype(operation)>;
      if constexpr (std::is_same_v<op_type, op_read>) {
        io_uring_prep_read(sqe_ptr, operation.fd, operation.buffer_data.data(), operation.buffer_data.size(), operation.offset);
      } else if constexpr (std::is_same_v<op_type, op_write>) {
        io_uring_prep_write(sqe_ptr, operation.fd, operation.buffer_data.data(), operation.buffer_data.size(), operation.offset);
      } else if constexpr (std::is_same_v<op_type, op_fsync>) {
        io_uring_prep_fsync(sqe_ptr, operation.fd, 0);
      } else if constexpr (std::is_same_v<op_type, op_open>) {
        io_uring_prep_openat(sqe_ptr, AT_FDCWD, operation.file_path, operation.open_flags, operation.open_mode);
      } else if constexpr (std::is_same_v<op_type, op_close>) {
        io_uring_prep_close(sqe_ptr, operation.fd);
      } else if constexpr (std::is_same_v<op_type, op_send_socket>) {
        io_uring_prep_send(sqe_ptr, operation.socket_fd, operation.buffer_data.data(), operation.buffer_data.size(), 0);
      } else if constexpr (std::is_same_v<op_type, op_receive_socket>) {
        io_uring_prep_recv(sqe_ptr, operation.socket_fd, operation.buffer_data.data(), operation.buffer_data.size(), 0);
      } else if constexpr (std::is_same_v<op_type, op_accept_socket>) {
        io_uring_prep_accept(sqe_ptr, operation.listen_socket_fd, nullptr, 0, 0);  // NOLINT(hicpp-use-nullptr): liburing API requires null for optional addr
      } else if constexpr (std::is_same_v<op_type, op_connect_socket>) {
        io_uring_prep_connect(sqe_ptr, operation.socket_fd,
                              reinterpret_cast<const struct sockaddr*>(operation.remote_address.data()),  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast): required by liburing API
                              static_cast<socklen_t>(operation.remote_address.size() / sizeof(std::byte)));
      }
    },
               entry_ref);

    io_uring_sqe_set_data(sqe_ptr, reinterpret_cast<void*>(static_cast<uintptr_t>(index)));  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast,performance-no-int-to-ptr): required by liburing API to store index as user data

    // remove index from non pushed
    _events_to_push.pop_back();

    events_to_submit++;
  }

  if (events_to_submit > 0) {
    int submitted = io_uring_submit(&_ring);
    ASYNC_CORO_ASSERT(events_to_submit == submitted);
  }

  // Phase 3: Wait for completions
  struct io_uring_cqe* cqe_ptr = nullptr;
  int n_cqes = 0;

  constexpr auto nanoseconds_per_second = 1000000000LL;

  if (max_wait.count() > 0) {
    struct __kernel_timespec timespec_val{};
    timespec_val.tv_sec = max_wait.count() / nanoseconds_per_second;
    timespec_val.tv_nsec = max_wait.count() % nanoseconds_per_second;
    n_cqes = io_uring_wait_cqe_timeout(&_ring, &cqe_ptr, &timespec_val);
    if (n_cqes != 0) {
      // n_cqes < 0: error, n_cqes > 0: timeout (no CQE found)
      return;
    }
  } else {
    n_cqes = io_uring_peek_cqe(&_ring, &cqe_ptr);
    if (n_cqes != 0) {
      // n_cqes < 0: error, n_cqes == -EAGAIN: no CQE available
      return;
    }
  }

  // Phase 4: Process CQEs
  while (cqe_ptr != nullptr) {
    const auto index = static_cast<size_t>(reinterpret_cast<uintptr_t>(io_uring_cqe_get_data(cqe_ptr)));  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast): required by liburing API to retrieve stored index

    const int result = cqe_ptr->res;

    auto& entry = _local_ring[index];

    // Update entry fields
    const auto is_error = (result < 0);
    core::error err = is_error ? core::error{core::error_type::system_error, errno} : core::error{};

    // Dispatch completion based on operation type.
    std::visit([result, is_error, err](auto& operation) {
      using op_type = std::decay_t<decltype(operation)>;
      if constexpr (std::is_same_v<op_type, op_read> || std::is_same_v<op_type, op_write> ||
                    std::is_same_v<op_type, op_send_socket> || std::is_same_v<op_type, op_receive_socket>) {
        if (is_error) {
          operation.callback(expected<size_t, core::error>{unexpect, err});
        } else {
          operation.callback(static_cast<size_t>(result));
        }
      } else if constexpr (std::is_same_v<op_type, op_open>) {
        if (is_error) {
          operation.callback(expected<int, core::error>{unexpect, err});
        } else {
          operation.callback(static_cast<int>(result));
        }
      } else if constexpr (std::is_same_v<op_type, op_fsync> || std::is_same_v<op_type, op_close>) {
        if (is_error) {
          operation.callback(expected<void, core::error>{unexpect, err});
        } else {
          operation.callback(expected<void, core::error>{});
        }
      } else if constexpr (std::is_same_v<op_type, op_accept_socket> || std::is_same_v<op_type, op_connect_socket>) {
        if constexpr (std::is_same_v<op_type, op_accept_socket>) {
          if (is_error) {
            operation.callback(expected<socket_type, core::error>{unexpect, err});
          } else {
            operation.callback(static_cast<socket_type>(result));
          }
        } else {
          if (is_error) {
            operation.callback(expected<void, core::error>{unexpect, err});
          } else {
            operation.callback(expected<void, core::error>{});
          }
        }
      } else {
        static_assert(async_coro::always_false<op_type>::value, "Unsupported operation type");
      }
    },
               entry);

    _free_indices.push_back(index);

    // Get next CQE
    io_uring_cqe_seen(&_ring, cqe_ptr);
    if (io_uring_peek_cqe(&_ring, &cqe_ptr) != 0) {
      cqe_ptr = nullptr;
    }
  }
}

void io_uring_reactor::submit_read(int file_descriptor, uint64_t offset, std::span<std::byte> buffer, continue_size_callback_t&& callback) {  // NOLINT(bugprone-easily-swappable-parameters): order matches POSIX read(fd, buf, len) semantics
  op_read read_op{};
  read_op.fd = file_descriptor;
  read_op.callback = std::move(callback);
  read_op.buffer_data = buffer;
  read_op.offset = offset;

  _requests.push(std::move(read_op));
}

void io_uring_reactor::submit_write(int file_descriptor, uint64_t offset, std::span<const std::byte> buffer, continue_size_callback_t&& callback) {  // NOLINT(bugprone-easily-swappable-parameters): order matches POSIX write(fd, buf, len) semantics
  op_write write_op{};
  write_op.fd = file_descriptor;
  write_op.callback = std::move(callback);
  // io_uring requires mutable buffers for write operations
  write_op.buffer_data = std::span<std::byte>{const_cast<std::byte*>(buffer.data()), buffer.size()};  // NOLINT(cppcoreguidelines-pro-type-const-cast): liburing API requires non-const buffer pointer
  write_op.offset = offset;

  _requests.push(std::move(write_op));
}

void io_uring_reactor::submit_fsync(int file_descriptor, continue_void_callback_t&& callback) {
  op_fsync fsync_op{};
  fsync_op.fd = file_descriptor;
  fsync_op.callback = std::move(callback);

  _requests.push(std::move(fsync_op));
}

void io_uring_reactor::submit_close(int file_descriptor, continue_void_callback_t&& callback) {
  op_close close_op{};
  close_op.fd = file_descriptor;
  close_op.callback = std::move(callback);

  _requests.push(std::move(close_op));
}

void io_uring_reactor::submit_open(const char* path, file_open_mode open_mode, int permissions, continue_file_callback_t&& callback) {  // NOLINT(bugprone-easily-swappable-parameters): order matches POSIX open(path, flags, mode) semantics
  int posix_flags = mode_to_posix_flags(open_mode);

  op_open open_op{};
  open_op.file_path = path;
  open_op.open_flags = posix_flags;
  open_op.open_mode = permissions;
  open_op.callback = std::move(callback);

  _requests.push(std::move(open_op));
}

expected<socket_type, core::error> io_uring_reactor::create_socket(socket_type_id kind) noexcept {  // NOLINT(readability-convert-member-functions-to-static): socket creation may need reactor state in future
  const int domain = AF_INET;
  const int type = (kind == socket_type_id::tcp) ? SOCK_STREAM : SOCK_DGRAM;
  const int protocol = 0;

  const socket_type sock = socket(domain, type, protocol);
  if (sock < 0) {
    return expected<socket_type, core::error>{unexpect, core::error{core::error_type::create_socket_failed, errno}};
  }

  return sock;
}

expected<void, core::error> io_uring_reactor::bind_socket(socket_type socket_handle, std::span<const std::byte> address) noexcept {  // NOLINT(readability-convert-member-functions-to-static): socket ops may need reactor state in future
  const int result = bind(socket_handle,
                          reinterpret_cast<const struct sockaddr*>(address.data()),  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast): required by POSIX bind() API
                          static_cast<socklen_t>(address.size() / sizeof(std::byte)));
  if (result < 0) {
    return expected<void, core::error>{unexpect, core::error{core::error_type::bind_failed, errno}};
  }
  return expected<void, core::error>{};
}

expected<void, core::error> io_uring_reactor::listen_socket(socket_type socket_handle, int backlog) noexcept {  // NOLINT(readability-convert-member-functions-to-static): socket ops may need reactor state in future
  const int result = listen(socket_handle, backlog);
  if (result < 0) {
    return expected<void, core::error>{unexpect, core::error{core::error_type::listen_failed, errno}};
  }
  return expected<void, core::error>{};
}

void io_uring_reactor::submit_send_socket(socket_type socket_handle, std::span<const std::byte> buffer, continue_size_callback_t&& callback) {
  op_send_socket send_op{};
  send_op.socket_fd = socket_handle;
  send_op.callback = std::move(callback);
  // io_uring requires mutable buffers for send operations
  send_op.buffer_data = std::span<std::byte>{const_cast<std::byte*>(buffer.data()), buffer.size()};  // NOLINT(cppcoreguidelines-pro-type-const-cast): liburing API requires non-const buffer pointer

  _requests.push(std::move(send_op));
}

void io_uring_reactor::submit_receive_socket(socket_type socket_handle, std::span<std::byte> buffer, continue_size_callback_t&& callback) {
  op_receive_socket recv_op{};
  recv_op.socket_fd = socket_handle;
  recv_op.callback = std::move(callback);
  recv_op.buffer_data = buffer;

  _requests.push(std::move(recv_op));
}

void io_uring_reactor::submit_accept_socket(socket_type listen_socket, continue_socket_callback_t&& callback) {
  op_accept_socket accept_op{};
  accept_op.listen_socket_fd = listen_socket;
  accept_op.callback = std::move(callback);

  _requests.push(std::move(accept_op));
}

void io_uring_reactor::submit_connect_socket(socket_type socket_handle, std::span<const std::byte> remote_address, continue_void_callback_t&& callback) {
  op_connect_socket connect_op{};
  connect_op.socket_fd = socket_handle;
  connect_op.callback = std::move(callback);
  connect_op.remote_address = remote_address;

  _requests.push(std::move(connect_op));
}

}  // namespace server::io

#endif  // IO_URING_ENABLED
