#pragma once

#if IO_URING_ENABLED

#include <async_coro/task.h>
#include <server/core/error.h>
#include <server/io/io_config.h>
#include <server/io/uring/io_uring_reactor.h>
#include <server/io/uring/io_uring_socket.h>
#include <server/utils/expected.h>

#include <string_view>

namespace server::io {

/**
 * @brief Async TCP listener using Linux io_uring.
 *
 * Provides non-blocking socket listen/accept operations that integrate with the
 * io_uring reactor. Uses io_uring's native accept operation for true async
 * connection acceptance.
 *
 * @note Requires Linux kernel 5.1+ with io_uring support.
 * @note The io_uring_reactor must outlive this listener.
 */
class io_uring_listener {
 public:
  // Non-copyable (reactor reference is fixed).
  io_uring_listener(const io_uring_listener&) = delete;
  io_uring_listener& operator=(const io_uring_listener&) = delete;

  // Movable
  io_uring_listener(io_uring_listener&& other) noexcept;
  io_uring_listener& operator=(io_uring_listener&& other) noexcept;

  ~io_uring_listener() noexcept;

  /**
   * @brief Create and open a new listener on the specified address.
   *
   * This is a static factory method that creates a listener, binds to the given IP:port,
   * sets SO_REUSEADDR, and calls listen().
   * The socket is associated with the io_uring reactor.
   *
   * @param reactor The io_uring reactor to use. Must outlive the returned listener.
   * @param ip_address IPv4 address string (e.g., "127.0.0.1").
   * @param port Port number.
   * @return An expected<io_uring_listener, core::error>. On success, contains the listener.
   *         On failure, contains an error.
   */
  [[nodiscard]] static expected<io_uring_listener, core::error> open(io_uring_reactor& reactor, std::string_view ip_address, uint16_t port);

  /**
   * @brief Accept a new connection asynchronously (coroutine version).
   *
   * Creates an accept socket via the reactor and initiates an async accept operation.
   *
   * @return An awaitable that resolves to an expected<io_uring_socket, core::error>.
   *         On success, contains the accepted socket. On failure, contains an error.
   */
  [[nodiscard]] async_coro::task<expected<io_uring_socket, core::error>> accept();

  /**
   * @brief Check if the listener is open.
   *
   * @return true if the listener has an open socket, false otherwise.
   */
  [[nodiscard]] bool is_open() const noexcept { return _sock != invalid_socket_id; }

  /**
   * @brief Get the listening socket descriptor.
   *
   * @return The socket descriptor, or -1 if the listener is closed.
   */
  [[nodiscard]] socket_type get_native_handle() const noexcept { return _sock; }

  /**
   * @brief Close the listener socket.
   *
   * @return An expected<void, core::error>. On success, contains void.
   *         On failure, contains an error.
   */
  [[nodiscard]] expected<void, core::error> close() noexcept;

 private:
  io_uring_listener(io_uring_reactor& reactor, socket_type sock) noexcept;

 private:
  io_uring_reactor& _reactor;  // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members): reactor lifetime guaranteed by owner
  socket_type _sock = invalid_socket_id;
};

}  // namespace server::io

#endif  // IO_URING_ENABLED
