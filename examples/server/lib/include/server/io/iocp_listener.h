#pragma once

#if WIN_IOCP_ENABLED

#include <async_coro/task.h>
#include <server/io/io_config.h>
#include <server/io/iocp_reactor.h>
#include <server/io/iocp_socket.h>
#include <server/utils/expected.h>

#include <string>
#include <vector>

namespace server::io {

/**
 * @brief Async TCP listener using Windows I/O Completion Ports (IOCP).
 *
 * Provides non-blocking socket listen/accept operations that integrate with the
 * IOCP reactor. Uses AcceptEx for true async connection acceptance.
 * All socket creation and WinAPI calls are handled by the reactor.
 *
 * @note Requires Windows with IOCP support.
 * @note The iocp_reactor must outlive this listener.
 */
class iocp_listener {
 public:
  // Non-copyable (reactor reference is fixed).
  iocp_listener(const iocp_listener&) = delete;
  iocp_listener& operator=(const iocp_listener&) = delete;

  // Movable
  iocp_listener(iocp_listener&& other) noexcept;
  iocp_listener& operator=(iocp_listener&& other) noexcept;

  ~iocp_listener();

  /**
   * @brief Create and open a new listener on the specified address.
   *
   * This is a static factory method that creates a listener, binds to the given IP:port,
   * sets SO_REUSEADDR, and calls listen().
   * The socket is associated with the IOCP completion port.
   *
   * @param reactor The IOCP reactor to use. Must outlive the returned listener.
   * @param ip_address IPv4 address string (e.g., "127.0.0.1").
   * @param port Port number.
   * @return An expected<iocp_listener, std::string>. On success, contains the listener.
   *         On failure, contains an error message.
   */
  [[nodiscard]] static expected<iocp_listener, std::string> open(iocp_reactor& reactor, std::string_view ip_address, uint16_t port);

  /**
   * @brief Accept a new connection asynchronously (coroutine version).
   *
   * Creates an accept socket via the reactor and initiates an async AcceptEx operation.
   *
   * @return An awaitable that resolves to an expected<iocp_socket, std::string>.
   *         On success, contains the accepted socket. On failure, contains an error message.
   */
  [[nodiscard]] async_coro::task<expected<iocp_socket, std::string>> accept();

  /**
   * @brief Check if the listener is open.
   *
   * @return true if the listener has an open socket, false otherwise.
   */
  [[nodiscard]] bool is_open() const noexcept { return _sock != invalid_socket_id; }

  /**
   * @brief Get the listening socket handle.
   *
   * @return The socket handle, or INVALID_SOCKET if the listener is closed.
   */
  [[nodiscard]] socket_type get_native_handle() const noexcept { return _sock; }

  /**
   * @brief Close the listener socket.
   *
   * @return An expected<void, std::string>. On success, contains void.
   *         On failure, contains an error message.
   */
  [[nodiscard]] expected<void, std::string> close();

 private:
  /**
   * @brief Construct an iocp_listener.
   *
   * @param reactor The IOCP reactor to use. Must outlive this listener.
   */
  explicit iocp_listener(iocp_reactor& reactor);

  iocp_reactor& _reactor;
  socket_type _sock = invalid_socket_id;
};

}  // namespace server::io

#endif  // WIN_IOCP_ENABLED
