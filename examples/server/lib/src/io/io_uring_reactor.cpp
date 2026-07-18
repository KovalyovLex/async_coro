#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <variant>
#if IO_URING_ENABLED

#include <async_coro/config.h>
#include <fcntl.h>
#include <liburing.h>
#include <linux/io_uring.h>
#include <server/io/io_uring_reactor.h>
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
  reactor._local_ring = std::make_unique<request_entry[]>(ring_size);
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

void io_uring_reactor::process_loop(std::chrono::nanoseconds max_wait) {
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
    sqe->user_data = static_cast<uint64_t>(index);

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

    // remove index from non pushed
    _events_to_push.pop_back();

    events_to_submit++;
  }

  if (events_to_submit > 0) {
    int submitted = io_uring_submit(&_ring);
    if (submitted < 0) {
      return;  // Submit failure — no completions to process.
    }
    ASYNC_CORO_ASSERT(events_to_submit == submitted);
  }

  // Phase 3: Wait for completions
  struct io_uring_cqe* cqe_ptr = nullptr;
  int n_cqes = 0;

  if (max_wait.count() > 0) {
    struct __kernel_timespec ts;
    ts.tv_sec = max_wait.count() / 1000000000LL;
    ts.tv_nsec = max_wait.count() % 1000000000LL;
    n_cqes = io_uring_wait_cqe_timeout(&_ring, &cqe_ptr, &ts);
    if (n_cqes <= 0) {
      return;
    }
  } else {
    n_cqes = io_uring_peek_cqe(&_ring, &cqe_ptr);
    if (n_cqes <= 0) {
      return;
    }
  }

  // Phase 4: Process CQEs
  while (cqe_ptr != nullptr) {
    const uint64_t user_data = cqe_ptr->user_data;
    const int result = cqe_ptr->res;

    auto index = static_cast<size_t>(user_data);

    auto& entry = _local_ring[index];

    // Update entry fields
    const auto is_error = (result < 0);
    const auto result_errno = is_error ? errno : 0;

    if (!is_error) {
      switch (entry.operation) {
        case operation_type::send_data: {
          std::visit([&](auto& var) {
            if constexpr (std::is_same_v<decltype(var), continue_size_callback_t>) {
              if (var) {
                var(expected<size_t, std::string>{static_cast<size_t>(result)});
              }
            }
          },
                     entry.callback);
        } break;
        case operation_type::receive_data:
          std::visit([&](auto& var) {
            if constexpr (std::is_same_v<decltype(var), continue_size_callback_t>) {
              if (var) {
                var(expected<size_t, std::string>{static_cast<size_t>(result)});
              }
            }
          },
                     entry.callback);
          break;
        case operation_type::open_file: {
          std::visit([&](auto& var) {
            if constexpr (std::is_same_v<decltype(var), continue_file_callback_t>) {
              if (var) {
                var(expected<int, std::string>{static_cast<int>(result)});
              }
            }
          },
                     entry.callback);
        } break;
        case operation_type::close_file:
        case operation_type::fsync: {
          std::visit([&](auto& var) {
            if constexpr (std::is_same_v<decltype(var), continue_void_callback_t>) {
              if (var) {
                var(expected<void, std::string>{});
              }
            }
          },
                     entry.callback);
        } break;
      }
    } else {
      auto error_msg = std::string(strerror(errno));
      std::visit([&](auto& var) {
        if constexpr (std::is_same_v<decltype(var), continue_size_callback_t>) {
          if (var) {
            var(unexpect, std::move(error_msg));
          }
        } else if constexpr (std::is_same_v<decltype(var), continue_file_callback_t>) {
          if (var) {
            var(unexpect, std::move(error_msg));
          }
        } else if constexpr (std::is_same_v<decltype(var), continue_void_callback_t>) {
          if (var) {
            var(unexpect, std::move(error_msg));
          }
        }
      },
                 entry.callback);
    }

    _free_indices.push_back(index);

    // Get next CQE
    io_uring_cqe_seen(&_ring, cqe_ptr);
    if (io_uring_peek_cqe(&_ring, &cqe_ptr) != 0) {
      cqe_ptr = nullptr;
    }
  }
}

void io_uring_reactor::submit_read(int fd, size_t offset, std::span<uint8_t> buffer, continue_size_callback_t&& callback) {
  request_entry entry;
  entry.fd = fd;
  entry.operation = operation_type::receive_data;
  entry.callback = std::move(callback);
  entry.buffer_data = buffer;
  entry.offset = offset;

  _requests.push(std::move(entry));
}

void io_uring_reactor::submit_write(int fd, size_t offset, std::span<const uint8_t> buffer, continue_size_callback_t&& callback) {
  request_entry entry;
  entry.fd = fd;
  entry.operation = operation_type::send_data;
  entry.callback = std::move(callback);
  entry.buffer_data = std::span<uint8_t>{const_cast<uint8_t*>(buffer.data()), buffer.size()};
  entry.offset = offset;

  _requests.push(std::move(entry));
}

void io_uring_reactor::submit_fsync(int fd, continue_void_callback_t&& callback) {
  request_entry entry;
  entry.fd = fd;
  entry.operation = operation_type::fsync;
  entry.callback = std::move(callback);

  _requests.push(std::move(entry));
}

void io_uring_reactor::submit_close(int fd, continue_void_callback_t&& callback) {
  request_entry entry;
  entry.fd = fd;
  entry.operation = operation_type::close_file;
  entry.callback = std::move(callback);

  _requests.push(std::move(entry));
}

void io_uring_reactor::submit_open(const char* path, int flags, int mode, continue_file_callback_t&& callback) {
  request_entry entry;
  entry.file_path = path;
  entry.open_flags = flags;
  entry.open_mode = mode;
  entry.operation = operation_type::open_file;
  entry.callback = std::move(callback);

  _requests.push(std::move(entry));
}

}  // namespace server::io

#endif  // IO_URING_ENABLED
