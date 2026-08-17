#pragma once

#include <server/io/io_config.h>

#if EPOLL_SOCKET

#include <async_coro/task.h>
#include <server/core/error.h>
#include <server/io/epoll/epoll_reactor.h>
#include <server/utils/expected.h>

#include <cstddef>
#include <span>

namespace server::io {

/**
 * @brief Async socket I/O operations using Linux epoll.
 *
 * Provides non-blocking socket send/receive operations that integrate with the
 * epoll reactor. This is the standard backend for Linux systems, offering
 * reliable async I/O through the epoll event loop.
 *
 * @note The epoll_reactor must outlive any epoll_socket registered with it.
 * @note Socket handles are created through the reactor (no direct socket() calls).
 */
class epoll_socket {
 public:
  /**
   * @brief Create a new client socket and connect to a remote address (coroutine version).
   *
   * Creates a TCP socket via the reactor, binds to any local address,
   * and initiates an async connect.
   *
   * @param reactor The epoll reactor to use. Must outlive this socket.
   * @param remote_address Buffer containing destination sockaddr structure.
   * @param address_length Length of the address structure in bytes.
   * @return An awaitable that resolves to an expected<epoll_socket, core::error>.
   *         On success, contains the connected socket. On failure, contains an error.
   */
  [[nodiscard]] static async_coro::task<expected<epoll_socket, core::error>> connect_coro(
      epoll_reactor& reactor, const void* remote_address, socklen_t address_length) noexcept;

  // Non-copyable to prevent multiple objects from closing the same socket descriptor.
  epoll_socket(const epoll_socket&) = delete;
  epoll_socket& operator=(const epoll_socket&) = delete;

  // Movable - ownership of the socket descriptor transfers; reactor reference is preserved.
  ~epoll_socket() noexcept;
  epoll_socket(epoll_socket&& other) noexcept;
  epoll_socket& operator=(epoll_socket&& other) noexcept;

  /**
   * @brief Send data over the socket.
   *
   * Sends in a loop until all data is sent (or error).
   *
   * @param data The data to send.
   * @return An awaitable that resolves to an expected<size_t, core::error>.
   *         On success, contains the total number of bytes sent.
   *         On failure, contains an error.
   */
  [[nodiscard]] async_coro::task<expected<size_t, core::error>> send(std::span<const std::byte> data);

  /**
   * @brief Receive data from the socket into a buffer.
   *
   * Receives available data into the provided buffer.
   * Returns the number of bytes actually received, which may be less than the buffer size.
   *
   * @param buffer The buffer to receive into.
   * @return An awaitable that resolves to an expected<size_t, core::error>.
   *         On success, contains the total number of bytes received.
   *         On failure, contains an error. Zero bytes indicates connection closed.
   */
  [[nodiscard]] async_coro::task<expected<size_t, core::error>> receive(std::span<std::byte> buffer);

  /**
   * @brief Close the socket synchronously.
   *
   * Closes the socket descriptor via close().
   * After this returns successfully, the socket is no longer valid for I/O.
   *
   * @return An expected<void, core::error>. On success, contains void.
   *         On failure, contains an error.
   */
  [[nodiscard]] expected<void, core::error> close() noexcept;

  /**
   * @brief Check if the socket is closed.
   *
   * @return true if the socket is closed, false otherwise.
   */
  [[nodiscard]] bool is_closed() const noexcept { return _sock == invalid_socket_id; }

  /**
   * @brief Get the socket descriptor.
   *
   * @return The socket descriptor, or -1 if the socket is closed.
   */
  [[nodiscard]] socket_type get_native_handle() const noexcept { return _sock; }

  /**
   * @brief Set TCP_NODELAY option.
   *
   * @param enable true to disable Nagle's algorithm, false to enable.
   * @return An expected<void, core::error>. On success, contains void.
   *         On failure, contains an error.
   */
  [[nodiscard]] expected<void, core::error> set_no_delay(bool enable) noexcept;

 private:
  /**
   * @brief Synchronously close the socket.
   *
   * Closes the socket descriptor via close() if still open.
   */
  expected<void, core::error> close_sync() noexcept;

  /**
   * @brief Construct an epoll_socket with an already-opened socket descriptor.
   *
   * @param reactor The epoll reactor this socket belongs to. Must outlive this object.
   * @param socket_handle The socket descriptor, already created and optionally connected.
   */
  epoll_socket(epoll_reactor& reactor, socket_type socket_handle, size_t index) noexcept;
  epoll_socket(epoll_reactor& reactor, socket_type socket_handle) noexcept;

  // Checks index and subscribes to reactor notifications if it wasn't already
  void check_subscribed();

  // Allow epoll_listener to construct epoll_socket from accepted sockets.
  friend class epoll_listener;

  // Allow reactor to control index
  friend class epoll_reactor;

 private:
  static constexpr size_t k_invalid_index = size_t(-1);

  epoll_reactor& _reactor;  // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members): reactor lifetime guaranteed by owner
  socket_type _sock = invalid_socket_id;
  size_t _index = k_invalid_index;
};

}  // namespace server::io

#endif  // EPOLL_SOCKET
