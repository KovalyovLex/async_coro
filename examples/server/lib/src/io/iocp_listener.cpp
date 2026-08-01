#if WIN_IOCP_ENABLED

#include <async_coro/await/await_callback.h>
#include <server/io/iocp_listener.h>
#include <server/io/iocp_reactor.h>
#include <server/utils/expected.h>

#include <cstddef>
#include <cstring>
#include <utility>
#include <vector>

// Windows socket headers for inet_pton.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

namespace server::io {

// ============================================================================
// Constructor / Destructor
// ============================================================================

iocp_listener::iocp_listener(iocp_reactor& reactor)
    : _reactor(reactor) {}

iocp_listener::~iocp_listener() {
  if (is_open()) {
    (void)close();
  }
}

// ============================================================================
// open
// ============================================================================

expected<void, std::string> iocp_listener::open(std::string_view ip_address, uint16_t port) {
  // Create a TCP socket via the reactor.
  auto sock_result = _reactor.create_socket(socket_type_id::tcp, IPPROTO_TCP);
  if (!sock_result) {
    return expected<void, std::string>{unexpect, std::move(sock_result).error()};
  }

  socket_type sock = sock_result.value();

  // Set SO_REUSEADDR to allow quick rebinding.
  int reuse_addr = 1;
  int result = setsockopt(
      sock,
      SOL_SOCKET,
      SO_REUSEADDR,
      reinterpret_cast<const char*>(&reuse_addr),
      sizeof(reuse_addr));
  if (result == SOCKET_ERROR) {
    (void)_reactor.close_socket(sock);
    return expected<void, std::string>{unexpect, std::string("setsockopt(SO_REUSEADDR) failed: ") + iocp_reactor::format_windows_error()};
  }

  // Convert IP address string to sockaddr_in.
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  if (inet_pton(AF_INET, ip_address.data(), &addr.sin_addr) != 1) {
    (void)_reactor.close_socket(sock);
    return expected<void, std::string>{unexpect, "inet_pton failed: invalid IP address"};
  }

  // Bind to the address.
  auto bind_result = _reactor.bind_socket(
      sock,
      std::span<const std::byte>(
          reinterpret_cast<const std::byte*>(&addr),
          sizeof(addr)));
  if (!bind_result) {
    (void)_reactor.close_socket(sock);
    return expected<void, std::string>{unexpect, std::move(bind_result).error()};
  }

  // Listen with default backlog.
  auto listen_result = _reactor.listen_socket(sock, SOMAXCONN);
  if (!listen_result) {
    (void)_reactor.close_socket(sock);
    return expected<void, std::string>{unexpect, std::move(listen_result).error()};
  }

  _sock = sock;
  return expected<void, std::string>{};
}

// ============================================================================
// accept
// ============================================================================

async_coro::task<expected<iocp_socket, std::string>> iocp_listener::accept() {
  if (!is_open()) {
    co_return expected<iocp_socket, std::string>{unexpect, "Listener is closed"};
  }

  // Create buffers for local and remote addresses.
  // AcceptEx requires each address buffer to have extra space beyond sizeof(sockaddr_in).
  // We need at least sizeof(sockaddr_in) + 16 bytes for each address.
  constexpr size_t k_addr_len = sizeof(sockaddr_in) + 16;

  // Single contiguous buffer: local_addr + remote_addr + recv_data
  std::array<std::byte, k_addr_len> local_recv_buf;
  std::array<std::byte, k_addr_len> remote_recv_buf;

  // Submit async accept operation (reactor creates the accept socket internally).
  auto result = co_await async_coro::await_callback_with_result<expected<socket_type, std::string>>(
      [&](auto cont) {
        // Local address starts at offset 0, remote address starts after local address
        _reactor.submit_accept_socket(
            _sock,
            local_recv_buf,    // local address buffer
            remote_recv_buf,   // remote address buffer
            {},                // extra receive buffer
            std::move(cont));  // callback
      });

  if (!result) {
    co_return expected<iocp_socket, std::string>{unexpect, std::move(result.error())};
  }

  co_return iocp_socket{_reactor, result.value()};
}

// ============================================================================
// close
// ============================================================================

expected<void, std::string> iocp_listener::close() {
  if (!is_open()) {
    return expected<void, std::string>{};
  }

  // Close via reactor to cancel all pending IO operations (e.g., in-flight accepts).
  auto result = _reactor.close_socket(std::exchange(_sock, invalid_socket_id));
  if (!result) {
    return expected<void, std::string>{unexpect, std::move(result).error()};
  }

  return expected<void, std::string>{};
}

}  // namespace server::io

#endif  // WIN_IOCP_ENABLED
