#pragma once

#include <server/io/io_config.h>

#if EPOLL_KQUEUE_ENABLED

#include <async_coro/task.h>
#include <server/core/error.h>
#include <server/io/epoll/epoll_reactor.h>
#include <server/io/epoll/epoll_socket.h>
#include <server/utils/expected.h>

#include <string_view>

namespace server::io {

/**
 * @brief Async TCP listener using Linux epoll or BSD kqueue.
 *
 * Provides non-blocking socket listen/accept operations that integrate with the
 * epoll/kqueue reactor. Uses epoll/kqueue to detect incoming connections and accepts them
 * asynchronously.
 *
 * @note The epoll_reactor must outlive this listener.
 */
class epoll_listener {
 public:
  // Non-copyable (reactor reference is fixed).
  epoll_listener(const epoll_listener&) = delete;
  epoll_listener& operator=(const epoll_listener&) = delete;

  // Movable
  epoll_listener(epoll_listener&& other) noexcept;
  epoll_listener& operator=(epoll_listener&& other) noexcept;

  ~epoll_listener() noexcept;

  /**
   * @brief Create and open a new listener on the specified address.
   *
   * This is a static factory method that creates a listener, binds to the given IP:port,
   * sets SO_REUSEADDR, and calls listen().
   * The socket is associated with the epoll reactor.
   *
   * @param reactor The epoll reactor to use. Must outlive the returned listener.
   * @param ip_address IPv4 address string (e.g., "127.0.0.1").
   * @param port Port number.
   * @return An expected<epoll_listener, core::error>. On success, contains the listener.
   *         On failure, contains an error.
   */
  [[nodiscard]] static expected<epoll_listener, core::error> open(epoll_reactor& reactor, std::string_view ip_address, uint16_t port);

  /**
   * @brief Accept a new connection asynchronously (coroutine version).
   *
   * Creates an accept socket via the reactor and initiates an async accept operation.
   *
   * @return An awaitable that resolves to an expected<epoll_socket, core::error>.
   *         On success, contains the accepted socket. On failure, contains an error.
   */
  [[nodiscard]] async_coro::task<expected<epoll_socket, core::error>> accept();

  /**
   * @brief Check if the listener is open.
   *
   * @return true if the listener has an open socket, false otherwise.
   */
  [[nodiscard]] bool is_open() const noexcept { return !_sock.is_closed(); }

  /**
   * @brief Get the listening socket descriptor.
   *
   * @return The socket descriptor, or -1 if the listener is closed.
   */
  [[nodiscard]] socket_type get_native_handle() const noexcept { return _sock.get_native_handle(); }

  /**
   * @brief Close the listener socket.
   *
   * @return An expected<void, core::error>. On success, contains void.
   *         On failure, contains an error.
   */
  [[nodiscard]] expected<void, core::error> close() noexcept;

 private:
  epoll_listener(epoll_reactor& reactor, socket_type sock) noexcept;

 private:
  epoll_socket _sock;
};

}  // namespace server::io

#endif  // EPOLL_KQUEUE_ENABLED
