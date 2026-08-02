#include <server/io/iocp/iocp_reactor.h>

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

// WinSock2 headers for socket I/O.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <mswsock.h>
#include <winsock2.h>
#include <ws2tcpip.h>

namespace server::io {

// ============================================================================
// AcceptEx / ConnectEx — cached in _winsock_extensions, queried once during create().
// No system calls in the hot path.
// ==============================================================================

template <typename OpType>
void iocp_reactor::dispatch_completion_for_op(DWORD bytes_transferred, bool success, OpType& op) noexcept {
  // Index recycling is handled by the caller in process_loop.
  if (!op.callback) {
    return;
  }

  if constexpr (std::is_same_v<OpType, op_read> || std::is_same_v<OpType, op_write> ||
                std::is_same_v<OpType, op_send_socket> || std::is_same_v<OpType, op_receive_socket>) {
    if (!success) {
      op.callback(expected<size_t, std::string>{unexpect, format_windows_error()});
    } else {
      op.callback(static_cast<size_t>(bytes_transferred));
    }
  } else if constexpr (std::is_same_v<OpType, op_open>) {
    if (!success) {
      op.callback(expected<file_handle_t, std::string>{unexpect, format_windows_error()});
    } else {
      op.callback(op.fd);
    }
  } else if constexpr (std::is_same_v<OpType, op_accept_socket>) {
    if (!success) {
      op.callback(expected<socket_type, std::string>{unexpect, format_windows_error()});
    } else {
      op.callback(op.accept_socket_fd);
    }
  } else if constexpr (std::is_same_v<OpType, op_connect_socket>) {
    if (!success) {
      op.callback(expected<void, std::string>{unexpect, format_windows_error()});
    } else {
      op.callback(expected<void, std::string>{});
    }
  }
  op.callback = nullptr;
}

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

  // Initialize WinSock and query AcceptEx / ConnectEx (lazy, one-time).
  auto ws_result = io::init_winsock();
  if (!ws_result) {
    return expected<iocp_reactor, std::string>{unexpect, std::move(ws_result).error()};
  }

  reactor._ring_size = ring_size;
  reactor._local_ring = std::make_unique<ring_entry[]>(ring_size);
  reactor._free_indices.reserve(ring_size);
  reactor._events_to_push.reserve(ring_size);
  reactor._winsock_extensions = *ws_result;

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
      _events_to_push(std::move(other._events_to_push)),
      _temp_w_path(std::move(other._temp_w_path)),
      _winsock_extensions(other._winsock_extensions) {
  other._completion_port = INVALID_HANDLE_VALUE;
  other._ring_size = 0;

  ASYNC_CORO_ASSERT(!other._requests.has_value());
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
    _temp_w_path = std::move(other._temp_w_path);
    _winsock_extensions = std::move(other._winsock_extensions);

    ASYNC_CORO_ASSERT(!other._requests.has_value());

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
    request_variant entry;
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

    std::visit([&](auto& op) {
      using op_type = std::decay_t<decltype(op)>;

      if constexpr (std::is_same_v<op_type, op_read>) {
        // Initialize the OVERLAPPED struct — guarantees correct lifetime for async I/O.
        entry.overlapped = {};
        entry.overlapped.Offset = static_cast<DWORD>(op.offset & 0xFFFFFFFF);
        entry.overlapped.OffsetHigh = static_cast<DWORD>((op.offset >> 32) & 0xFFFFFFFF);

        DWORD bytes_written = 0;
        BOOL read_result = ReadFile(
            op.fd,
            op.buffer_data.data(),
            static_cast<DWORD>(op.buffer_data.size()),
            &bytes_written,
            &entry.overlapped);

        if (read_result) {
          dispatch_completion_for_op(bytes_written, true, op);
        } else {
          const auto error = GetLastError();
          if (error == ERROR_IO_PENDING) {
            return;  // Asynchronous pending — will complete via IOCP notification.
          } else if (error == ERROR_HANDLE_EOF) {
            dispatch_completion_for_op(0, true, op);
          } else {
            dispatch_completion_for_op(0, false, op);
          }
        }
        _free_indices.push_back(index);

      } else if constexpr (std::is_same_v<op_type, op_write>) {
        entry.overlapped = {};
        entry.overlapped.Offset = static_cast<DWORD>(op.offset & 0xFFFFFFFF);
        entry.overlapped.OffsetHigh = static_cast<DWORD>((op.offset >> 32) & 0xFFFFFFFF);

        DWORD bytes_written = 0;
        BOOL write_result = WriteFile(
            op.fd,
            op.buffer_data.data(),
            static_cast<DWORD>(op.buffer_data.size()),
            &bytes_written,
            &entry.overlapped);

        if (write_result) {
          dispatch_completion_for_op(bytes_written, true, op);
        } else if (GetLastError() == ERROR_IO_PENDING) {
          return;  // Asynchronous pending.
        } else {
          dispatch_completion_for_op(0, false, op);
        }
        _free_indices.push_back(index);

      } else if constexpr (std::is_same_v<op_type, op_send_socket>) {
        SOCKET sock = op.socket_fd;

        DWORD bytes_sent = 0;
        DWORD flags = 0;
        entry.overlapped = {};

        BOOL send_result = WSASend(
                               sock,
                               &op.wsa_buf,
                               1,
                               &bytes_sent,
                               flags,
                               &entry.overlapped,
                               nullptr) == 0;

        if (send_result) {
          dispatch_completion_for_op(bytes_sent, true, op);
        } else {
          const auto error = WSAGetLastError();
          if (error == WSA_IO_PENDING) {
            return;
          } else {
            dispatch_completion_for_op(0, false, op);
          }
        }
        _free_indices.push_back(index);

      } else if constexpr (std::is_same_v<op_type, op_receive_socket>) {
        SOCKET sock = op.socket_fd;

        DWORD bytes_recv = 0;
        DWORD flags = 0;
        entry.overlapped = {};

        BOOL recv_result = WSARecv(
                               sock,
                               &op.wsa_buf,
                               1,
                               &bytes_recv,
                               &flags,
                               &entry.overlapped,
                               nullptr) == 0;

        if (recv_result) {
          dispatch_completion_for_op(bytes_recv, true, op);
        } else {
          const auto error = WSAGetLastError();
          if (error == WSA_IO_PENDING) {
            return;
          } else if (error == WSAECONNRESET || error == WSAECONNABORTED) {
            dispatch_completion_for_op(0, true, op);
          } else {
            dispatch_completion_for_op(0, false, op);
          }
        }
        _free_indices.push_back(index);

      } else if constexpr (std::is_same_v<op_type, op_accept_socket>) {
        SOCKET listen_sock = op.listen_socket_fd;
        SOCKET accept_sock = op.accept_socket_fd;

        entry.overlapped = {};

        BOOL accept_result = _winsock_extensions.accept_ex(
            listen_sock,
            accept_sock,
            op.local_address_buffer.data(),
            static_cast<DWORD>(op.buffer_data.size()),
            static_cast<DWORD>(op.local_address_buffer.size()),
            static_cast<DWORD>(op.remote_address_buffer.size()),
            nullptr,
            &entry.overlapped);

        if (accept_result) {
          dispatch_completion_for_op(0, true, op);
        } else {
          const auto error = WSAGetLastError();
          if (error == WSA_IO_PENDING) {
            return;
          } else {
            dispatch_completion_for_op(0, false, op);
          }
        }
        _free_indices.push_back(index);

      } else if constexpr (std::is_same_v<op_type, op_connect_socket>) {
        SOCKET sock = op.socket_fd;

        entry.overlapped = {};

        BOOL connect_result = _winsock_extensions.connect_ex(
            sock,
            reinterpret_cast<const sockaddr*>(op.remote_address.data()),
            static_cast<int>(op.remote_address.size() / sizeof(std::byte)),
            nullptr,
            0,
            nullptr,
            &entry.overlapped);

        if (connect_result) {
          dispatch_completion_for_op(0, true, op);
        } else {
          const auto error = WSAGetLastError();
          if (error == WSA_IO_PENDING) {
            return;
          } else {
            dispatch_completion_for_op(0, false, op);
          }
        }
        _free_indices.push_back(index);

      } else if constexpr (std::is_same_v<op_type, op_open>) {
        int wide_len = MultiByteToWideChar(CP_UTF8, 0, op.file_path, -1, nullptr, 0);
        if (wide_len <= 0) {
          dispatch_completion_for_op(0, false, op);
          _free_indices.push_back(index);
          return;
        }

        _temp_w_path.resize(wide_len);
        MultiByteToWideChar(CP_UTF8, 0, op.file_path, -1, _temp_w_path.data(), wide_len);

        DWORD access = 0;
        DWORD creation_disposition = 0;
        mode_to_win_flags(op.open_flags, access, creation_disposition);

        HANDLE file_handle = CreateFileW(
            _temp_w_path.data(),
            access,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            creation_disposition,
            FILE_FLAG_OVERLAPPED,
            nullptr);

        if (file_handle == INVALID_HANDLE_VALUE) {
          dispatch_completion_for_op(0, false, op);
        } else {
          HANDLE iocp_handle = CreateIoCompletionPort(
              file_handle,
              _completion_port,
              0,
              0);

          if (iocp_handle == nullptr) {
            dispatch_completion_for_op(0, false, op);
            (void)::server::io::close_file(file_handle);
          } else {
            op.fd = file_handle;
            dispatch_completion_for_op(0, true, op);
          }
        }
        _free_indices.push_back(index);
      }
    },
               entry.request);
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

      // Determine success based on operation type.
      bool op_succeeded = result == TRUE;

      // File reads: EOF is treated as success.
      if (!op_succeeded && std::holds_alternative<op_read>(entry.request)) {
        op_succeeded = (GetLastError() == ERROR_HANDLE_EOF);
      }

      std::visit([&](auto& op) {
        using op_type = std::decay_t<decltype(op)>;

        dispatch_completion_for_op(bytes_transferred, op_succeeded, op);
      },
                 entry.request);

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
// Submit operations
// ============================================================================

void iocp_reactor::submit_read(file_handle_t file_descriptor, uint64_t offset, std::span<std::byte> buffer, continue_size_callback_t&& callback) {
  op_read op{file_descriptor, offset, buffer, std::move(callback)};
  _requests.push(std::move(op));
}

void iocp_reactor::submit_write(file_handle_t file_descriptor, uint64_t offset, std::span<const std::byte> buffer, continue_size_callback_t&& callback) {
  // IOCP requires mutable buffers for write operations.
  op_write op{file_descriptor, offset,
              std::span<std::byte>{const_cast<std::byte*>(buffer.data()), buffer.size()},  // NOLINT(cppcoreguidelines-pro-type-const-cast): Windows API requires non-const buffer pointer
              std::move(callback)};
  _requests.push(std::move(op));
}

expected<void, std::string> iocp_reactor::flush(file_handle_t file_descriptor) noexcept {
  BOOL flush_result = FlushFileBuffers(file_descriptor);
  if (!flush_result) {
    return expected<void, std::string>{unexpect, format_windows_error()};
  }
  return expected<void, std::string>{};
}

expected<void, std::string> iocp_reactor::close_file(file_handle_t file_descriptor) noexcept {
  if (!::server::io::close_file(file_descriptor)) {
    return expected<void, std::string>{unexpect, format_windows_error()};
  }
  return expected<void, std::string>{};
}

bool iocp_reactor::cancel_socket_io(socket_type socket_handle) noexcept {
  if (socket_handle == invalid_socket_id) {
    return true;
  }

  // shutdown(SD_BOTH) aborts both sends and receives, causing any pending
  // overlapped operations to complete with an error via IOCP.
  int result = shutdown(socket_handle, SD_BOTH);
  return result != SOCKET_ERROR;
}

expected<void, std::string> iocp_reactor::close_socket(socket_type socket_handle) noexcept {
  if (socket_handle == invalid_socket_id) {
    return expected<void, std::string>{};
  }

  // Cancel all pending IO operations first.
  (void)cancel_socket_io(socket_handle);

  // Then close the socket handle.
  if (!io::close_socket(socket_handle)) {
    return expected<void, std::string>{unexpect, format_windows_error()};
  }

  return expected<void, std::string>{};
}

void iocp_reactor::submit_open(const char* path, file_open_mode open_mode, continue_file_callback_t&& callback) {
  op_open op{invalid_file_handle, path, open_mode, std::move(callback)};
  _requests.push(std::move(op));
}

expected<socket_type, std::string> iocp_reactor::create_socket(socket_type_id kind) noexcept {
  int addr_family = AF_INET;
  const int socket_type_val = (kind == socket_type_id::tcp) ? SOCK_STREAM : SOCK_DGRAM;
  const int socket_proto_val = (kind == socket_type_id::tcp) ? IPPROTO_TCP : IPPROTO_UDP;

  SOCKET sock = WSASocket(addr_family, socket_type_val, socket_proto_val, nullptr, 0, WSA_FLAG_OVERLAPPED);
  if (sock == INVALID_SOCKET) {
    return expected<socket_type, std::string>{unexpect, std::string("WSASocket failed: ") + format_windows_error()};
  }

  // Associate socket with IOCP completion port.
  if (CreateIoCompletionPort(
          reinterpret_cast<HANDLE>(sock),
          _completion_port,
          0,
          0) == nullptr) {
    io::close_socket(sock);
    return expected<socket_type, std::string>{unexpect, std::string("CreateIoCompletionPort failed: ") + format_windows_error()};
  }

  return static_cast<socket_type>(sock);
}

expected<void, std::string> iocp_reactor::bind_socket(socket_type socket_handle, std::span<const std::byte> address) noexcept {
  int result = bind(
      socket_handle,
      reinterpret_cast<const sockaddr*>(address.data()),
      static_cast<int>(address.size()));

  if (result == SOCKET_ERROR) {
    return expected<void, std::string>{unexpect, std::string("bind failed: ") + format_windows_error()};
  }

  return expected<void, std::string>{};
}

expected<void, std::string> iocp_reactor::listen_socket(socket_type socket_handle, int backlog) noexcept {
  int result = listen(
      socket_handle,
      backlog);

  if (result == SOCKET_ERROR) {
    return expected<void, std::string>{unexpect, std::string("listen failed: ") + format_windows_error()};
  }

  return expected<void, std::string>{};
}

void iocp_reactor::submit_send_socket(socket_type socket_handle, std::span<const std::byte> buffer, continue_size_callback_t&& callback) {
  op_send_socket op{};
  op.socket_fd = socket_handle;
  op.wsa_buf.buf = const_cast<char*>(reinterpret_cast<const char*>(buffer.data()));
  op.wsa_buf.len = static_cast<ULONG>(buffer.size());
  op.callback = std::move(callback);
  _requests.push(std::move(op));
}

void iocp_reactor::submit_receive_socket(socket_type socket_handle, std::span<std::byte> buffer, continue_size_callback_t&& callback) {
  op_receive_socket op{};
  op.socket_fd = socket_handle;
  op.wsa_buf.buf = reinterpret_cast<char*>(buffer.data());
  op.wsa_buf.len = static_cast<ULONG>(buffer.size());
  op.callback = std::move(callback);
  _requests.push(std::move(op));
}

void iocp_reactor::submit_accept_socket(socket_type listen_socket,
                                        std::span<std::byte> local_address_buffer,
                                        std::span<std::byte> remote_address_buffer,
                                        std::span<std::byte> buffer_data,
                                        continue_socket_callback_t&& callback) {
  // Create the accept socket internally.
  auto accept_sock_result = create_socket(socket_type_id::tcp);
  if (!accept_sock_result) {
    if (callback) {
      callback(expected<socket_type, std::string>{unexpect, std::move(accept_sock_result).error()});
    }
    return;
  }

  socket_type accept_sock = accept_sock_result.value();

  op_accept_socket op{listen_socket, accept_sock, local_address_buffer, remote_address_buffer, buffer_data, std::move(callback)};
  _requests.push(std::move(op));
}

void iocp_reactor::submit_connect_socket(socket_type socket_handle, std::span<const std::byte> remote_address, continue_void_callback_t&& callback) {
  op_connect_socket op{socket_handle, remote_address, std::move(callback)};
  _requests.push(std::move(op));
}

}  // namespace server::io

#endif  // WIN_IOCP_ENABLED
