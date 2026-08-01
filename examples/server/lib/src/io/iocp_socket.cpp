#if WIN_IOCP_ENABLED

#include <async_coro/await/await_callback.h>
#include <server/io/iocp_reactor.h>
#include <server/io/iocp_socket.h>
#include <server/utils/expected.h>

#include <cstddef>
#include <utility>
#include <vector>

namespace server::io {

// ============================================================================
// Constructor / Destructor / Move semantics
// ============================================================================

iocp_socket::iocp_socket(iocp_reactor& reactor, socket_type socket_handle) noexcept
    : _reactor(reactor),
      _sock(socket_handle) {
  // The socket is already created via WSASocket in connect_coro/accept_coro.
}

iocp_socket::~iocp_socket() {
  close_sync();
}

iocp_socket::iocp_socket(iocp_socket&& other) noexcept
    : _reactor(other._reactor),
      _sock(std::exchange(other._sock, invalid_socket_id)) {
}

iocp_socket& iocp_socket::operator=(iocp_socket&& other) {
  if (this != &other) {
    // Explicitly destroy current object (closes socket handle via destructor).
    this->~iocp_socket();

    // Reconstruct in-place using placement new to rebind the reactor reference.
    ::new (static_cast<void*>(this)) iocp_socket(std::move(other));
  }
  return *this;
}

void iocp_socket::close_sync() {
  if (is_closed()) {
    return;
  }

  // Use close_socket for sockets (calls closesocket on Windows).
  (void)close_socket(std::exchange(_sock, invalid_socket_id));
}

// ============================================================================
// connect_coro
// ============================================================================

async_coro::task<expected<iocp_socket, std::string>> iocp_socket::connect_coro(
    iocp_reactor& reactor, const void* remote_address, socklen_t address_length) noexcept {
  // Create a TCP socket via the reactor.
  auto sock_result = reactor.create_socket(iocp_reactor::socket_kind::stream, IPPROTO_TCP);
  if (!sock_result) {
    co_return expected<iocp_socket, std::string>{unexpect, std::move(sock_result).error()};
  }

  socket_type sock = sock_result.value();

  // Bind to any local address — required by ConnectEx before obtaining the function pointer.
  sockaddr_in bind_addr{};
  bind_addr.sin_family = AF_INET;
  bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  bind_addr.sin_port = 0;  // Let the system assign a port
  auto bind_result = reactor.bind_socket(
      sock,
      std::span<const std::byte>(
          reinterpret_cast<const std::byte*>(&bind_addr),
          sizeof(bind_addr)));
  if (!bind_result) {
    (void)close_socket(sock);
    co_return expected<iocp_socket, std::string>{unexpect, std::move(bind_result).error()};
  }

  // Submit async connect operation.
  auto result = co_await async_coro::await_callback_with_result<expected<void, std::string>>(
      [&](auto cont) {
        reactor.submit_connect_socket(
            sock,
            std::span<const std::byte>(
                reinterpret_cast<const std::byte*>(remote_address),
                static_cast<size_t>(address_length)),
            std::move(cont));
      });

  if (!result) {
    (void)close_socket(sock);
    co_return expected<iocp_socket, std::string>{unexpect, std::move(result).error()};
  }

  co_return iocp_socket{reactor, sock};
}

// ============================================================================
// accept_coro
// ============================================================================

async_coro::task<expected<iocp_socket, std::string>> iocp_socket::accept_coro(
    iocp_reactor& reactor, socket_type listen_socket) noexcept {
  // Create buffers for local/remote addresses (sockaddr_in is 16 bytes).
  std::vector<std::byte> local_addr_buf(16);
  std::vector<std::byte> remote_addr_buf(16);
  std::vector<std::byte> recv_buf(0);  // Empty receive buffer for AcceptEx

  // Submit async accept operation (reactor creates the accept socket internally).
  auto result = co_await async_coro::await_callback_with_result<expected<file_handle_t, std::string>>(
      [&](auto cont) {
        reactor.submit_accept_socket(
            listen_socket,
            local_addr_buf,
            remote_addr_buf,
            recv_buf,
            std::move(cont));
      });

  if (!result) {
    co_return expected<iocp_socket, std::string>{unexpect, std::move(result).error()};
  }

  co_return iocp_socket{reactor, reinterpret_cast<socket_type>(result.value())};
}

// ============================================================================
// send
// ============================================================================

async_coro::task<expected<size_t, std::string>> iocp_socket::send(std::span<const std::byte> data) {
  if (is_closed()) {
    co_return expected<size_t, std::string>{unexpect, "Socket is closed"};
  }

  size_t total_bytes_sent = 0;
  auto current_data = data;

  while (total_bytes_sent < data.size()) {
    // Submit async send operation.
    auto result = co_await async_coro::await_callback_with_result<expected<size_t, std::string>>(
        [this, &current_data](auto cont) {
          _reactor.submit_send_socket(
              _sock,
              std::span<std::byte>(
                  const_cast<std::byte*>(reinterpret_cast<const std::byte*>(current_data.data())),
                  current_data.size()),
              std::move(cont));
        });

    if (!result) {
      co_return expected<size_t, std::string>{unexpect, std::move(result).error()};
    }

    const auto sent = result.value();
    total_bytes_sent += sent;
    current_data = current_data.subspan(sent);
  }

  co_return total_bytes_sent;
}

// ============================================================================
// receive
// ============================================================================

async_coro::task<expected<size_t, std::string>> iocp_socket::receive(std::span<std::byte> buffer) {
  if (is_closed()) {
    co_return expected<size_t, std::string>{unexpect, "Socket is closed"};
  }

  size_t total_bytes_received = 0;
  auto current_buffer = buffer;

  while (total_bytes_received < buffer.size()) {
    // Submit async receive operation.
    auto result = co_await async_coro::await_callback_with_result<expected<size_t, std::string>>(
        [this, &current_buffer](auto cont) {
          _reactor.submit_receive_socket(_sock, current_buffer, std::move(cont));
        });

    if (!result) {
      co_return expected<size_t, std::string>{unexpect, std::move(result).error()};
    }

    const auto received = result.value();
    if (received == 0) {
      break;  // Connection closed
    }
    total_bytes_received += received;
    current_buffer = current_buffer.subspan(received);
  }

  co_return total_bytes_received;
}

// ============================================================================
// close
// ============================================================================

expected<void, std::string> iocp_socket::close() {
  if (is_closed()) {
    return expected<void, std::string>{};
  }

  // Use close_socket for sockets (calls closesocket on Windows).
  if (!close_socket(std::exchange(_sock, invalid_socket_id))) {
    return expected<void, std::string>{unexpect, iocp_reactor::format_windows_error()};
  }

  return expected<void, std::string>{};
}

// ============================================================================
// set_no_delay
// ============================================================================

expected<void, std::string> iocp_socket::set_no_delay(bool enable) {
  if (is_closed()) {
    return expected<void, std::string>{unexpect, "Socket is closed"};
  }

  int val = enable ? 1 : 0;
  int result = setsockopt(
      _sock,
      IPPROTO_TCP,
      TCP_NODELAY,
      reinterpret_cast<const char*>(&val),
      static_cast<int>(sizeof(val)));

  if (result != 0) {
    return expected<void, std::string>{unexpect, "setsockopt TCP_NODELAY failed"};
  }

  return expected<void, std::string>{};
}

}  // namespace server::io

#endif  // WIN_IOCP_ENABLED
