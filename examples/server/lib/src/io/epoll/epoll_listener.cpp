#include <server/io/io_config.h>

#if EPOLL_SOCKET

#include <async_coro/await/await_callback.h>
#include <server/core/error.h>
#include <server/io/epoll/epoll_listener.h>
#include <server/io/epoll/epoll_reactor.h>
#include <server/utils/expected.h>

// Linux socket headers
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <utility>

namespace server::io {

// ============================================================================
// Constructor / Destructor
// ============================================================================

epoll_listener::epoll_listener(epoll_reactor& reactor, socket_type sock) noexcept
    : _sock(reactor, sock) {}

epoll_listener::~epoll_listener() noexcept = default;

// ============================================================================
// Move semantics
// ============================================================================

epoll_listener::epoll_listener(epoll_listener&& other) noexcept = default;

epoll_listener& epoll_listener::operator=(epoll_listener&& other) noexcept = default;

// ============================================================================
// open
// ============================================================================

expected<epoll_listener, core::error> epoll_listener::open(epoll_reactor& reactor, std::string_view ip_address, uint16_t port) {
  // Create a TCP socket via the reactor.
  auto sock_result = reactor.create_socket(socket_type_id::tcp);
  if (!sock_result) {
    return expected<epoll_listener, core::error>{unexpect, std::move(sock_result).error()};
  }

  socket_type sock = sock_result.value();

  // Set SO_REUSEADDR to allow quick rebinding.
  int reuse_addr = 1;
  const int result = setsockopt(
      sock,
      SOL_SOCKET,
      SO_REUSEADDR,
      reinterpret_cast<const char*>(&reuse_addr),  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast): required by POSIX setsockopt() API
      sizeof(reuse_addr));
  if (result < 0) {
    close_socket(sock);
    return expected<epoll_listener, core::error>{unexpect, core::error{core::error_type::setsockopt_failed, errno}};  // NOLINT(readability-redundant-casting): suppress unused result warning from submit_close
  }

  // Convert IP address string to sockaddr_in.
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  if (inet_pton(AF_INET, ip_address.data(), &addr.sin_addr) != 1) {  // NOLINT(bugprone-suspicious-stringview-data-usage): inet_pton takes a C string, not a span
    close_socket(sock);
    return expected<epoll_listener, core::error>{unexpect, core::error_type::inet_pton_failed};
  }

  // Bind to the address.
  auto bind_result = reactor.bind_socket(
      sock,
      std::span<const std::byte>(
          reinterpret_cast<const std::byte*>(&addr),  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast): required by POSIX bind() API
          sizeof(addr)));
  if (!bind_result) {
    close_socket(sock);
    return expected<epoll_listener, core::error>{unexpect, std::move(bind_result).error()};
  }

  // Listen with default backlog.
  auto listen_result = reactor.listen_socket(sock, SOMAXCONN);
  if (!listen_result) {
    close_socket(sock);
    return expected<epoll_listener, core::error>{unexpect, std::move(listen_result).error()};
  }

  epoll_listener listener{reactor, sock};
  return listener;
}

// ============================================================================
// accept
// ============================================================================

async_coro::task<expected<epoll_socket, core::error>> epoll_listener::accept() {
  if (!is_open()) {
    co_return expected<epoll_socket, core::error>{unexpect, core::error_type::listener_closed};
  }

  // Perform the actual accept.
  sockaddr_in client_addr{};
  socklen_t addr_len = sizeof(client_addr);
  const socket_type accepted_sock = ::accept(_sock.get_native_handle(), reinterpret_cast<sockaddr*>(&client_addr), &addr_len);

  if (accepted_sock < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
    // Would block — wait for accept readiness notification from reactor
    auto result = co_await async_coro::await_callback_with_result<expected<socket_type, core::error>>(
        [&](auto cont) {
          _sock._reactor.submit_accept_socket(_sock, std::move(cont));
        });

    if (!result) {
      co_return expected<epoll_socket, core::error>{unexpect, std::move(result).error()};
    }

    // Retry accept after notification
    const socket_type retry_sock = ::accept(_sock.get_native_handle(), reinterpret_cast<sockaddr*>(&client_addr), &addr_len);
    if (retry_sock < 0) {
      co_return expected<epoll_socket, core::error>{unexpect, core::error{core::error_type::accept_failed, errno}};
    }

    // Set non-blocking mode on the accepted socket.
    const int flags = fcntl(retry_sock, F_GETFL, 0);
    if (flags >= 0) {
      fcntl(retry_sock, F_SETFL, flags | O_NONBLOCK);
    }

    co_return epoll_socket{_sock._reactor, retry_sock};
  }

  // Actual error
  if (accepted_sock < 0) {
    co_return expected<epoll_socket, core::error>{unexpect, core::error{core::error_type::accept_failed, errno}};
  }

  // Set non-blocking mode on the accepted socket.
  const int flags = fcntl(accepted_sock, F_GETFL, 0);
  if (flags >= 0) {
    fcntl(accepted_sock, F_SETFL, flags | O_NONBLOCK);
  }

  co_return epoll_socket{_sock._reactor, accepted_sock};
}

// ============================================================================
// close
// ============================================================================

expected<void, core::error> epoll_listener::close() noexcept {
  return _sock.close_sync();
}

}  // namespace server::io

#endif  // EPOLL_SOCKET
