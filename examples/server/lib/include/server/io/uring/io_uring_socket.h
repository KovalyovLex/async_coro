#pragma once

#if IO_URING_ENABLED

#include <async_coro/task.h>
#include <server/core/error.h>
#include <server/io/io_config.h>
#include <server/io/uring/io_uring_reactor.h>
#include <server/utils/expected.h>

#include <cstddef>
#include <span>

namespace server::io {

/**
 * @brief Async socket I/O operations using Linux io_uring.
 *
 * Provides non-blocking socket send/receive operations that integrate with the
 * io_uring reactor. This is the high-performance backend for Linux 5.1+
 * systems, offering lower latency compared to epoll-based sockets.
 *
 * @note Requires Linux kernel 5.1+ with io_uring support.
 * @note The io_uring_reactor must outlive any io_uring_socket registered with it.
 * @note Socket handles are created through the reactor (no direct socket() calls).
 */
class io_uring_socket {
 public:
  /**
   * @brief Create a new client socket and connect to a remote address (coroutine version).
   *
   * Creates a TCP socket via the reactor, binds to any local address,
   * and initiates an async connect.
   *
   * @param reactor The io_uring reactor to use. Must outlive this socket.
   * @param remote_address Buffer containing destination sockaddr structure.
   * @param address_length Length of the address structure in bytes.
   * @return An awaitable that resolves to an expected<io_uring_socket, core::error>.
   *         On success, contains the connected socket. On failure, contains an error.
   */
  [[nodiscard]] static async_coro::task<expected<io_uring_socket, core::error>> connect_coro(
      io_uring_reactor& reactor, const void* remote_address, socklen_t address_length) noexcept;

  // Non-copyable to prevent multiple objects from closing the same socket descriptor.
  io_uring_socket(const io_uring_socket&) = delete;
  io_uring_socket& operator=(const io_uring_socket&) = delete;

  // Movable - ownership of the socket descriptor transfers; reactor reference is preserved.
  ~io_uring_socket() noexcept;
  io_uring_socket(io_uring_socket&& other) noexcept;
  io_uring_socket& operator=(io_uring_socket&& other) noexcept;

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
  void close_sync() noexcept;

  /**
   * @brief Construct an io_uring_socket with an already-opened socket descriptor.
   *
   * @param reactor The io_uring reactor this socket belongs to. Must outlive this object.
   * @param socket_handle The socket descriptor, already created and optionally connected.
   */
  explicit io_uring_socket(io_uring_reactor& reactor, socket_type socket_handle) noexcept;

  // Allow io_uring_listener to construct io_uring_socket from accepted sockets.
  friend class io_uring_listener;

 private:
  io_uring_reactor& _reactor;  // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members): reactor lifetime guaranteed by owner
  socket_type _sock = invalid_socket_id;
};

}  // namespace server::io

#endif  // IO_URING_ENABLED
