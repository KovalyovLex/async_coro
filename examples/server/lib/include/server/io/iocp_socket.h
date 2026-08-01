#pragma once

#if WIN_IOCP_ENABLED

#include <async_coro/task.h>
#include <server/io/io_config.h>
#include <server/io/iocp_reactor.h>
#include <server/utils/expected.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

// Windows socket headers — already included via io_config.h when WIN_SOCKET is defined.
#if WIN_SOCKET
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

namespace server::io {

/**
 * @brief Async socket I/O operations using Windows I/O Completion Ports (IOCP).
 *
 * Provides non-blocking socket send/receive operations that integrate with the
 * IOCP reactor. This is the high-performance backend for Windows systems,
 * offering overlapped I/O with worker thread processing.
 *
 * @note Requires Windows with IOCP support.
 * @note The iocp_reactor must outlive any iocp_socket registered with it.
 * @note Socket handles are created through the reactor (no direct WinAPI calls).
 */
class iocp_socket {
 public:
  /**
   * @brief Create a new client socket and connect to a remote address (coroutine version).
   *
   * Creates a TCP socket via the reactor, associates it with IOCP, binds to any local address,
   * and initiates an async connect.
   *
   * @param reactor The IOCP reactor to use. Must outlive this socket.
   * @param remote_address Buffer containing destination sockaddr structure.
   * @param address_length Length of the address structure in bytes.
   * @return An awaitable that resolves to an expected<iocp_socket, std::string>.
   *         On success, contains the connected socket. On failure, contains an error message.
   */
  [[nodiscard]] static async_coro::task<expected<iocp_socket, std::string>> connect_coro(
      iocp_reactor& reactor, const void* remote_address, socklen_t address_length) noexcept;

  /**
   * @brief Accept a new connection on a listening socket (coroutine version).
   *
   * Creates an accept socket via the reactor and initiates an async AcceptEx operation.
   *
   * @param reactor The IOCP reactor to use. Must outlive this socket.
   * @param listen_socket The listening socket handle.
   * @return An awaitable that resolves to an expected<iocp_socket, std::string>.
   *         On success, contains the accepted socket. On failure, contains an error message.
   */
  [[nodiscard]] static async_coro::task<expected<iocp_socket, std::string>> accept_coro(
      iocp_reactor& reactor, socket_type listen_socket) noexcept;

  // Non-copyable to prevent multiple objects from closing the same socket handle.
  iocp_socket(const iocp_socket&) = delete;
  iocp_socket& operator=(const iocp_socket&) = delete;

  // Movable - ownership of the socket handle transfers; reactor reference is preserved.
  ~iocp_socket();
  iocp_socket(iocp_socket&& other) noexcept;
  iocp_socket& operator=(iocp_socket&& other);

  /**
   * @brief Send data over the socket.
   *
   * Sends in a loop until all data is sent (or error).
   *
   * @param data The data to send.
   * @return An awaitable that resolves to an expected<size_t, std::string>.
   *         On success, contains the total number of bytes sent.
   *         On failure, contains an error message.
   */
  [[nodiscard]] async_coro::task<expected<size_t, std::string>> send(std::span<const std::byte> data);

  /**
   * @brief Receive data from the socket into a buffer.
   *
   * Receives in a loop until the entire buffer is filled (or error/connection closed).
   *
   * @param buffer The buffer to receive into.
   * @return An awaitable that resolves to an expected<size_t, std::string>.
   *         On success, contains the total number of bytes received.
   *         On failure, contains an error message. Zero bytes indicates connection closed.
   */
  [[nodiscard]] async_coro::task<expected<size_t, std::string>> receive(std::span<std::byte> buffer);

  /**
   * @brief Close the socket synchronously.
   *
   * Closes the socket handle via closesocket().
   * After this returns successfully, the socket is no longer valid for I/O.
   *
   * @return An expected<void, std::string>. On success, contains void.
   *         On failure, contains an error message.
   */
  [[nodiscard]] expected<void, std::string> close();

  /**
   * @brief Check if the socket is closed.
   *
   * @return true if the socket is closed, false otherwise.
   */
  [[nodiscard]] bool is_closed() const noexcept { return _sock == invalid_socket_id; }

  /**
   * @brief Get the socket handle.
   *
   * @return The socket handle, or INVALID_SOCKET if the socket is closed.
   */
  [[nodiscard]] socket_type get_fd() const noexcept { return _sock; }

  /**
   * @brief Set TCP_NODELAY option.
   *
   * @param enable true to disable Nagle's algorithm, false to enable.
   * @return An expected<void, std::string>. On success, contains void.
   *         On failure, contains an error message.
   */
  [[nodiscard]] expected<void, std::string> set_no_delay(bool enable);

 private:
  /**
   * @brief Synchronously close the socket.
   *
   * Closes the socket handle via closesocket() if still open.
   */
  void close_sync();

  /**
   * @brief Construct an iocp_socket with an already-opened socket handle.
   *
   * @param reactor The IOCP reactor this socket belongs to. Must outlive this object.
   * @param socket_handle The socket handle, already created and optionally connected.
   */
  explicit iocp_socket(iocp_reactor& reactor, socket_type socket_handle) noexcept;

  // Allow iocp_listener to construct iocp_socket from accepted sockets.
  friend class iocp_listener;

  iocp_reactor& _reactor;  // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members): reactor lifetime guaranteed by owner
  socket_type _sock = invalid_socket_id;
};

}  // namespace server::io

#endif  // WIN_IOCP_ENABLED
