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
#include <server/io/uring/io_uring_reactor.h>
#include <server/utils/expected.h>

#include <cerrno>
#include <cstring>
#include <span>
#include <string>

namespace server::io {

io_uring_reactor::io_uring_reactor() noexcept = default;

expected<io_uring_reactor, std::string> io_uring_reactor::create(size_t ring_size) noexcept {
  io_uring_reactor reactor;
  reactor._ring_size = ring_size;
  reactor._local_ring = std::make_unique<request_entry[]>(ring_size);  // NOLINT(*-c-arrays): io_uring requires contiguous heap allocation managed by unique_ptr
  reactor._free_indices.reserve(ring_size);
  reactor._events_to_push.reserve(ring_size);

  for (size_t i = 0; i < ring_size; i++) {
    reactor._free_indices.push_back(i);
  }

  struct io_uring_params params{};

  if (io_uring_queue_init_params(ring_size, &reactor._ring, &params) != 0) {
    return expected<io_uring_reactor, std::string>{unexpect, std::string("io_uring_queue_init_params failed: ") + strerror(errno)};
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
    request_entry entry;
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

    io_uring_sqe* sqe = io_uring_get_sqe(&_ring);
    if (sqe == nullptr) {
      // Ring full — leave remaining entries in atomic_queue for next update call
      break;
    }

    // Submit SQE
    auto& entry = _local_ring[index];

    switch (entry.operation) {
      case operation_type::receive_data:
        io_uring_prep_read(sqe, entry.fd, entry.buffer_data.data(), entry.buffer_data.size(), entry.offset);
        break;
      case operation_type::send_data:
        io_uring_prep_write(sqe, entry.fd, entry.buffer_data.data(), entry.buffer_data.size(), entry.offset);
        break;
      case operation_type::fsync:
        io_uring_prep_fsync(sqe, entry.fd, 0);
        break;
      case operation_type::open_file:
        io_uring_prep_openat(sqe, AT_FDCWD, entry.file_path, entry.open_flags, entry.open_mode);
        break;
      case operation_type::close_file:
        io_uring_prep_close(sqe, entry.fd);
        break;
    }

    io_uring_sqe_set_data(sqe, reinterpret_cast<void*>(static_cast<uintptr_t>(index)));  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast): required by liburing API to store index as user data

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
    auto error_msg = is_error ? std::string{strerror(errno)} : std::string{};

    std::visit([&](auto& var) {
      if (!var) {
        return;
      }

      using T = std::decay_t<decltype(var)>;

      if constexpr (std::is_same_v<T, continue_size_callback_t>) {
        if (is_error) {
          var(expected<size_t, std::string>{unexpect, std::move(error_msg)});
        } else {
          var(static_cast<size_t>(result));
        }
      } else if constexpr (std::is_same_v<T, continue_file_callback_t>) {
        if (is_error) {
          var(expected<int, std::string>{unexpect, std::move(error_msg)});
        } else {
          var(static_cast<int>(result));
        }
      } else if constexpr (std::is_same_v<T, continue_void_callback_t>) {
        if (is_error) {
          var(expected<void, std::string>{unexpect, std::move(error_msg)});
        } else {
          var(expected<void, std::string>{});
        }
      } else {
        static_assert(async_coro::always_false<T>::value, "Unsupported callback type");
      }
    },
               entry.callback);

    _free_indices.push_back(index);

    // Get next CQE
    io_uring_cqe_seen(&_ring, cqe_ptr);
    if (io_uring_peek_cqe(&_ring, &cqe_ptr) != 0) {
      cqe_ptr = nullptr;
    }
  }
}

void io_uring_reactor::submit_read(int file_descriptor, uint64_t offset, std::span<std::byte> buffer, continue_size_callback_t&& callback) {  // NOLINT(bugprone-easily-swappable-parameters): order matches POSIX read(fd, buf, len) semantics
  request_entry entry;
  entry.fd = file_descriptor;
  entry.operation = operation_type::receive_data;
  entry.callback = std::move(callback);
  entry.buffer_data = buffer;
  entry.offset = offset;

  _requests.push(std::move(entry));
}

void io_uring_reactor::submit_write(int file_descriptor, uint64_t offset, std::span<const std::byte> buffer, continue_size_callback_t&& callback) {  // NOLINT(bugprone-easily-swappable-parameters): order matches POSIX write(fd, buf, len) semantics
  request_entry entry;
  entry.fd = file_descriptor;
  entry.operation = operation_type::send_data;
  entry.callback = std::move(callback);
  // io_uring requires mutable buffers for write operations
  entry.buffer_data = std::span<uint8_t>{const_cast<uint8_t*>(buffer.data()), buffer.size()};  // NOLINT(cppcoreguidelines-pro-type-const-cast): liburing API requires non-const buffer pointer
  entry.offset = offset;

  _requests.push(std::move(entry));
}

void io_uring_reactor::submit_fsync(int file_descriptor, continue_void_callback_t&& callback) {
  request_entry entry;
  entry.fd = file_descriptor;
  entry.operation = operation_type::fsync;
  entry.callback = std::move(callback);

  _requests.push(std::move(entry));
}

void io_uring_reactor::submit_close(int file_descriptor, continue_void_callback_t&& callback) {
  request_entry entry;
  entry.fd = file_descriptor;
  entry.operation = operation_type::close_file;
  entry.callback = std::move(callback);

  _requests.push(std::move(entry));
}

void io_uring_reactor::submit_open(const char* path, file_open_mode open_mode, int permissions, continue_file_callback_t&& callback) {  // NOLINT(bugprone-easily-swappable-parameters): order matches POSIX open(path, flags, mode) semantics
  int posix_flags = mode_to_posix_flags(open_mode);

  request_entry entry;
  entry.file_path = path;
  entry.open_flags = posix_flags;
  entry.open_mode = permissions;
  entry.operation = operation_type::open_file;
  entry.callback = std::move(callback);

  _requests.push(std::move(entry));
}

}  // namespace server::io

#endif  // IO_URING_ENABLED
