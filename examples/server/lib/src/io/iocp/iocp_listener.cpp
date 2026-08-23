#if WIN_IOCP_ENABLED

#include <async_coro/await/await_callback.h>
#include <server/core/error.h>
#include <server/io/io_config.h>
#include <server/io/iocp/iocp_listener.h>
#include <server/io/iocp/iocp_reactor.h>
#include <server/io/utils.h>
#include <server/utils/expected.h>

#include <cstddef>
#include <cstring>
#include <utility>
#include <vector>

// Windows socket headers for inet_pton.
#include <ws2tcpip.h>

namespace server::io {

// ============================================================================
// Constructor / Destructor
// ============================================================================

iocp_listener::iocp_listener(iocp_reactor& reactor, socket_type sock) noexcept
    : _reactor(reactor),
      _sock(sock) {}

iocp_listener::~iocp_listener() noexcept {
  (void)close();
}

// ============================================================================
// Move semantics
// ============================================================================

iocp_listener::iocp_listener(iocp_listener&& other) noexcept
    : _reactor(other._reactor),
      _sock(std::exchange(other._sock, invalid_socket_id)) {}

iocp_listener& iocp_listener::operator=(iocp_listener&& other) noexcept {
  if (this != &other) {
    // Explicitly destroy current object (closes socket via destructor).
    this->~iocp_listener();

    // Reconstruct in-place using placement new to rebind the reactor reference.
    ::new (static_cast<void*>(this)) iocp_listener(std::move(other));
  }
  return *this;
}

// ============================================================================
// open
// ============================================================================

expected<iocp_listener, core::error> iocp_listener::open(iocp_reactor& reactor, std::string_view ip_address, uint16_t port) {
  // Create a TCP socket via the reactor.
  auto sock_result = reactor.create_socket(socket_type_id::tcp);
  if (!sock_result) {
    return expected<iocp_listener, core::error>{unexpect, std::move(sock_result).error()};
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
    (void)reactor.close_socket(sock);
    return expected<iocp_listener, core::error>{unexpect, core::error{core::error_type::setsockopt_failed, static_cast<int>(WSAGetLastError())}};
  }

  // Convert IP address string to sockaddr_in.
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  if (inet_pton(AF_INET, ip_address.data(), &addr.sin_addr) != 1) {
    (void)reactor.close_socket(sock);
    return expected<iocp_listener, core::error>{unexpect, core::error_type::inet_pton_failed};
  }

  // Bind to the address.
  auto bind_result = reactor.bind_socket(
      sock,
      std::span<const std::byte>(
          reinterpret_cast<const std::byte*>(&addr),
          sizeof(addr)));
  if (!bind_result) {
    (void)reactor.close_socket(sock);
    return expected<iocp_listener, core::error>{unexpect, std::move(bind_result).error()};
  }

  // Listen with default backlog.
  auto listen_result = reactor.listen_socket(sock, SOMAXCONN);
  if (!listen_result) {
    (void)reactor.close_socket(sock);
    return expected<iocp_listener, core::error>{unexpect, std::move(listen_result).error()};
  }

  iocp_listener listener{reactor, sock};
  return listener;
}

// ============================================================================
// accept
// ============================================================================

async_coro::task<expected<iocp_socket, core::error>> iocp_listener::accept() {
  if (!is_open()) {
    co_return expected<iocp_socket, core::error>{unexpect, core::error_type::listener_closed};
  }

  // Create buffers for local and remote addresses.
  // AcceptEx requires each address buffer to have extra space beyond sizeof(sockaddr_in).
  // We need at least sizeof(sockaddr_in) + 16 bytes for each address.
  constexpr size_t k_addr_len = sizeof(sockaddr_in) + 16;

  // Store buffers in a lambda-captured struct to ensure they live until the async operation completes.
  // The callback is stored in the reactor and may be called asynchronously.
  struct buffers_t {
    std::array<std::byte, k_addr_len> local_addr;
    std::array<std::byte, k_addr_len> remote_addr;
  } buffers;

  // Submit async accept operation (reactor creates the accept socket internally).
  auto result = co_await async_coro::await_callback_with_result<expected<socket_type, core::error>>(
      [&](auto cont) {
        _reactor.submit_accept_socket(
            _sock,
            std::span<std::byte>(buffers.local_addr),
            std::span<std::byte>(buffers.remote_addr),
            {},
            std::move(cont));
      });

  if (!result) {
    co_return expected<iocp_socket, core::error>{unexpect, std::move(result).error()};
  }

  co_return iocp_socket{_reactor, result.value()};
}

// ============================================================================
// close
// ============================================================================

expected<void, core::error> iocp_listener::close() noexcept {
  if (!is_open()) {
    return expected<void, core::error>{};
  }

  // Close via reactor to cancel all pending IO operations (e.g., in-flight accepts).
  auto result = _reactor.close_socket(std::exchange(_sock, invalid_socket_id));
  if (!result) {
    return expected<void, core::error>{unexpect, std::move(result).error()};
  }

  return expected<void, core::error>{};
}

}  // namespace server::io

#endif  // WIN_IOCP_ENABLED
