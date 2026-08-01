#include <server/io/iocp_reactor.h>

#if WIN_IOCP_ENABLED

#include <async_coro/config.h>
#include <server/utils/expected.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

namespace server::io {

// ============================================================================
// Helper functions
// ============================================================================

std::string iocp_reactor::format_windows_error() noexcept {
  wchar_t msg[256];
  DWORD error_code = GetLastError();
  DWORD chars = FormatMessageW(
      FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
      nullptr,
      error_code,
      MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
      msg,
      static_cast<DWORD>(std::size(msg)),
      nullptr);

  if (chars > 0) {
    return wide_to_utf8(msg, static_cast<int>(chars));
  }
  return "Unknown Windows error (code: " + std::to_string(error_code) + ")";
}

std::string iocp_reactor::wide_to_utf8(const wchar_t* wide, int length) noexcept {
  if (!wide || length <= 0) {
    return {};
  }

  // Strip trailing newline/whitespace
  while (length > 0 && (wide[length - 1] == L'\n' || wide[length - 1] == L'\r')) {
    --length;
  }

  if (length <= 0) {
    return {};
  }

  int utf8_len = WideCharToMultiByte(CP_UTF8, 0, wide, length, nullptr, 0, nullptr, nullptr);
  if (utf8_len <= 0) {
    return {};
  }

  std::string utf8;
  utf8.resize(static_cast<size_t>(utf8_len));
  WideCharToMultiByte(CP_UTF8, 0, wide, length, utf8.data(), utf8_len, nullptr, nullptr);
  return utf8;
}

// ============================================================================
// Constructor / Destructor / Move semantics
// ============================================================================

iocp_reactor::iocp_reactor() noexcept = default;

expected<iocp_reactor, std::string> iocp_reactor::create(size_t ring_size) noexcept {
  iocp_reactor reactor;
  reactor._ring_size = ring_size;
  reactor._local_ring = std::make_unique<ring_entry[]>(ring_size);
  reactor._free_indices.reserve(ring_size);
  reactor._events_to_push.reserve(ring_size);

  for (size_t i = 0; i < ring_size; ++i) {
    reactor._free_indices.push_back(i);
  }

  // Create IOCP completion port with dummy handle.
  // The INVALID_HANDLE_VALUE creates a "port-only" completion port — we'll associate
  // file handles later via CreateIoCompletionPort(file_handle, port, key, 0).
  reactor._completion_port = CreateIoCompletionPort(
      INVALID_HANDLE_VALUE,
      nullptr,
      0,
      0);  // 0 threads means system default thread pool

  if (reactor._completion_port == nullptr) {
    return expected<iocp_reactor, std::string>{
        unexpect,
        std::string("CreateIoCompletionPort failed: ") + reactor.format_windows_error()};
  }

  return std::move(reactor);
}

iocp_reactor::iocp_reactor(iocp_reactor&& other) noexcept
    : _completion_port(other._completion_port),
      _ring_size(other._ring_size),
      _local_ring(std::move(other._local_ring)),
      _free_indices(std::move(other._free_indices)),
      _events_to_push(std::move(other._events_to_push)) {
  other._completion_port = INVALID_HANDLE_VALUE;
  other._ring_size = 0;
}

iocp_reactor& iocp_reactor::operator=(iocp_reactor&& other) noexcept {
  if (this != &other) {
    // Clean up existing completion port.
    if (_completion_port != INVALID_HANDLE_VALUE) {
      CloseHandle(_completion_port);
    }

    _completion_port = other._completion_port;
    _ring_size = other._ring_size;
    _local_ring = std::move(other._local_ring);
    _free_indices = std::move(other._free_indices);
    _events_to_push = std::move(other._events_to_push);

    other._completion_port = INVALID_HANDLE_VALUE;
    other._ring_size = 0;
  }
  return *this;
}

iocp_reactor::~iocp_reactor() noexcept {
  if (_completion_port != INVALID_HANDLE_VALUE) {
    CloseHandle(_completion_port);
  }
}

// ============================================================================
// process_loop — mirrors io_uring_reactor's 4-phase pattern
// ============================================================================

void iocp_reactor::process_loop(std::chrono::milliseconds max_wait) {  // NOLINT(readability-function-cognitive-complexity): complex but well-structured 4-phase IOCP processing loop
  // Phase 1: Drain atomic_queue into local ring buffer.
  while (!_free_indices.empty()) {
    request_entry entry;
    if (!_requests.try_pop(entry)) {
      break;
    }
    const auto index = _free_indices.back();
    _free_indices.pop_back();

    _local_ring[index].request = std::move(entry);
    _events_to_push.push_back(index);
  }

  // Phase 2: Submit overlapped I/O operations.
  while (!_events_to_push.empty()) {
    const auto index = _events_to_push.back();
    _events_to_push.pop_back();
    auto& entry = _local_ring[index];

    DWORD bytes_written = 0;

    switch (entry.request.operation) {
      case operation_type::receive_data: {
        // Initialize the OVERLAPPED struct embedded in request_entry.
        // This guarantees correct lifetime — Windows accesses it asynchronously.
        entry.overlapped = {};  // Zero-initialize.
        entry.overlapped.Offset = static_cast<DWORD>(entry.request.offset & 0xFFFFFFFF);
        entry.overlapped.OffsetHigh = static_cast<DWORD>((entry.request.offset >> 32) & 0xFFFFFFFF);

        BOOL read_result = ReadFile(
            entry.request.fd,
            entry.request.buffer_data.data(),
            static_cast<DWORD>(entry.request.buffer_data.size()),
            &bytes_written,
            &entry.overlapped);

        if (read_result) {
          // Synchronous completion — I/O finished immediately.
          // No completion packet is queued to IOCP; dispatch directly.
          dispatch_completion(index, bytes_written, true);
        } else {
          const auto error = GetLastError();
          if (error == ERROR_IO_PENDING) {
            // Asynchronous pending — will complete via IOCP notification.
            break;
          } else if (error == ERROR_HANDLE_EOF) {
            // EOF reached — treat as successful zero-byte read.
            dispatch_completion(index, 0, true);
          } else {
            // Actual error — dispatch failure immediately.
            dispatch_completion(index, 0, false);
          }
        }
        _free_indices.push_back(index);
        break;
      }

      case operation_type::send_data: {
        // Initialize the OVERLAPPED struct embedded in request_entry.
        entry.overlapped = {};  // Zero-initialize.
        entry.overlapped.Offset = static_cast<DWORD>(entry.request.offset & 0xFFFFFFFF);
        entry.overlapped.OffsetHigh = static_cast<DWORD>((entry.request.offset >> 32) & 0xFFFFFFFF);

        BOOL write_result = WriteFile(
            entry.request.fd,
            entry.request.buffer_data.data(),
            static_cast<DWORD>(entry.request.buffer_data.size()),
            &bytes_written,
            &entry.overlapped);

        if (write_result) {
          // Synchronous completion — I/O finished immediately.
          // No completion packet is queued to IOCP; dispatch directly.
          dispatch_completion(index, bytes_written, true);

        } else if (GetLastError() == ERROR_IO_PENDING) {
          // Asynchronous pending — will complete via IOCP notification.
          break;
        } else {
          // Actual error — dispatch failure immediately.
          dispatch_completion(index, 0, false);
        }
        _free_indices.push_back(index);
        break;
      }

      case operation_type::open_file: {
        // Convert UTF-8 path to wide string.
        int wide_len = MultiByteToWideChar(CP_UTF8, 0, entry.request.file_path, -1, nullptr, 0);
        if (wide_len <= 0) {
          dispatch_completion(index, 0, false);
          _free_indices.push_back(index);
          break;
        }

        _temp_w_path.resize(wide_len);
        MultiByteToWideChar(CP_UTF8, 0, entry.request.file_path, -1, _temp_w_path.data(), wide_len);

        DWORD access = 0;
        DWORD creation_disposition = 0;
        mode_to_win_flags(entry.request.open_flags, access, creation_disposition);

        HANDLE file_handle = CreateFileW(
            _temp_w_path.data(),
            access,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            creation_disposition,
            FILE_FLAG_OVERLAPPED,  // Required for async I/O
            nullptr);

        if (file_handle == INVALID_HANDLE_VALUE) {
          dispatch_completion(index, 0, false);
        } else {
          // Associate the file handle with IOCP BEFORE any overlapped I/O operations.
          // This is required by Windows - the handle must be registered with the
          // completion port before ReadFile/WriteFile can use it for async I/O.
          HANDLE iocp_handle = CreateIoCompletionPort(
              file_handle,
              _completion_port,
              0,
              0);

          if (iocp_handle == nullptr) {
            // Association failed - close the handle and report error.
            dispatch_completion(index, 0, false);

            (void)close_file(file_handle);
          } else {
            // Store the handle back into the entry.
            entry.request.fd = file_handle;

            dispatch_completion(index, 0, true);
          }
        }
        _free_indices.push_back(index);
        break;
      }
    }
  }

  // Phase 3: Wait for completion events via GetQueuedCompletionStatus.
  OVERLAPPED* overlapped_ptr = nullptr;
  DWORD bytes_transferred = 0;
  ULONG_PTR completion_key = 0;

  BOOL result = GetQueuedCompletionStatus(
      _completion_port,
      &bytes_transferred,
      &completion_key,
      &overlapped_ptr,
      static_cast<DWORD>(max_wait.count()));

  // Phase 4: Process completed operations.
  // GetQueuedCompletionStatus returns TRUE on success, FALSE on timeout/error in queue operation or I\O operation.
  // Even on timeout, there might be completions available.
  while (overlapped_ptr != nullptr) {
    // Retrieve the index from the OVERLAPPED struct's hEvent field.
    const size_t index = (reinterpret_cast<const char*>(overlapped_ptr) - reinterpret_cast<const char*>(&_local_ring[0].overlapped)) / sizeof(ring_entry);  // NOLINT(*-reinterpret-cast)
    ASYNC_CORO_ASSERT(index < _ring_size);

    if (index < _ring_size) {
      auto& entry = _local_ring[index];
      // read with EOF count successful
      const auto op_succeeded = result == TRUE || (entry.request.operation == operation_type::receive_data && GetLastError() == ERROR_HANDLE_EOF);
      dispatch_completion(index, bytes_transferred, op_succeeded);
      _free_indices.push_back(index);
    }

    // Check for more completions without blocking.
    overlapped_ptr = nullptr;
    result = GetQueuedCompletionStatus(
        _completion_port,
        &bytes_transferred,
        &completion_key,
        &overlapped_ptr,
        0);  // 0ms timeout = non-blocking peek
  }
}

// ============================================================================
// dispatch_completion — invokes callback and recycles index
// ============================================================================

void iocp_reactor::dispatch_completion(size_t index, DWORD bytes_transferred, bool success) noexcept {
  auto& entry = _local_ring[index];

  std::string error_msg;
  if (!success) {
    error_msg = format_windows_error();
  }

  std::visit([&](auto& var) {
    using T = std::decay_t<decltype(var)>;

    if constexpr (std::is_same_v<T, continue_size_callback_t>) {
      if (!var) {
        return;
      }
      if (!success) {
        var(expected<size_t, std::string>{unexpect, std::move(error_msg)});
      } else {
        var(static_cast<size_t>(bytes_transferred));
      }
    } else if constexpr (std::is_same_v<T, continue_file_callback_t>) {
      if (!var) {
        return;
      }
      if (!success) {
        var(expected<file_handle_t, std::string>{unexpect, std::move(error_msg)});
      } else {
        var(entry.request.fd);
      }
    } else if constexpr (std::is_same_v<T, continue_void_callback_t>) {
      if (!var) {
        return;
      }
      if (!success) {
        var(expected<void, std::string>{unexpect, std::move(error_msg)});
      } else {
        var(expected<void, std::string>{});
      }
    } else {
      static_assert(async_coro::always_false<T>::value, "Unsupported callback type");
    }
  },
             entry.request.callback);

  // Recycle the index for reuse.
  _free_indices.push_back(index);
}

// ============================================================================
// Submit operations
// ============================================================================

void iocp_reactor::submit_read(file_handle_t file_descriptor, uint64_t offset, std::span<std::byte> buffer, continue_size_callback_t&& callback) {
  request_entry entry;
  entry.fd = file_descriptor;
  entry.operation = operation_type::receive_data;
  entry.callback = std::move(callback);
  entry.buffer_data = buffer;
  entry.offset = offset;

  _requests.push(std::move(entry));
}

void iocp_reactor::submit_write(file_handle_t file_descriptor, uint64_t offset, std::span<const std::byte> buffer, continue_size_callback_t&& callback) {
  request_entry entry;
  entry.fd = file_descriptor;
  entry.operation = operation_type::send_data;
  entry.callback = std::move(callback);
  // IOCP requires mutable buffers for write operations.
  entry.buffer_data = std::span<std::byte>{const_cast<std::byte*>(buffer.data()), buffer.size()};  // NOLINT(cppcoreguidelines-pro-type-const-cast): Windows API requires non-const buffer pointer
  entry.offset = offset;

  _requests.push(std::move(entry));
}

expected<void, std::string> iocp_reactor::flush(file_handle_t file_descriptor) noexcept {
  BOOL flush_result = FlushFileBuffers(file_descriptor);
  if (!flush_result) {
    return expected<void, std::string>{unexpect, format_windows_error()};
  }
  return expected<void, std::string>{};
}

expected<void, std::string> iocp_reactor::close(file_handle_t file_descriptor) noexcept {
  if (!close_file(file_descriptor)) {
    return expected<void, std::string>{unexpect, format_windows_error()};
  }
  return expected<void, std::string>{};
}

void iocp_reactor::submit_open(const char* path, file_open_mode open_mode, continue_file_callback_t&& callback) {
  request_entry entry;
  entry.file_path = path;
  entry.open_flags = open_mode;
  entry.operation = operation_type::open_file;
  entry.callback = std::move(callback);

  _requests.push(std::move(entry));
}

}  // namespace server::io

#endif  // WIN_IOCP_ENABLED
