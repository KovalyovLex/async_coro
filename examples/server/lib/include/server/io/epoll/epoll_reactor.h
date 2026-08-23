#pragma once

#include <server/io/io_config.h>

#if EPOLL_KQUEUE_ENABLED

#include <async_coro/internal/await_callback.h>
#include <async_coro/thread_safety/analysis.h>
#include <async_coro/thread_safety/mutex.h>
#include <async_coro/utils/unique_function.h>
#include <server/core/error.h>
#include <server/io/socket_type_id.h>
#include <server/utils/expected.h>
#include <sys/socket.h>

#include <chrono>
#include <cstdint>
#include <variant>
#include <vector>

namespace server::io {

class epoll_socket;

/**
 * @brief epoll/kqueue-based event reactor for sockets.
 *
 * This reactor provides an interface for polling socket file descriptors using
 * the Linux epoll or BSD kqueue subsystem. It supports async send/receive/accept/connect
 * operations on TCP sockets.
 *
 * @note Requires Linux kernel 2.6+ (epoll) or BSD/macOS (kqueue).
 * @note The reactor must outlive any socket that is registered with it.
 * @note This class is not copyable or movable to prevent accidental sharing.
 */
class epoll_reactor {
 public:
  enum class connection_state : uint8_t {
    available_read,
    available_write,
    closed,
  };

  using continue_callback_t = async_coro::unique_function<void(connection_state), sizeof(async_coro::internal::await_continue_callback<connection_state>)>;

  /**
   * @brief Callback type for socket completion events. Returns number of bytes written/read or error.
   */
  using continue_size_callback_t = async_coro::unique_function<void(expected<size_t, core::error>)>;

  /**
   * @brief Callback type for socket completion events. Returns success or error.
   */
  using continue_void_callback_t = async_coro::unique_function<void(expected<void, core::error>)>;

  /**
   * @brief Callback type for socket accept events. Returns socket descriptor or error.
   */
  using continue_socket_callback_t = async_coro::unique_function<void(expected<socket_type, core::error>)>;

  epoll_reactor() noexcept;
  epoll_reactor(const epoll_reactor&) = delete;
  epoll_reactor(epoll_reactor&&) = delete;

  ~epoll_reactor() noexcept;

  epoll_reactor& operator=(const epoll_reactor&) = delete;
  epoll_reactor& operator=(epoll_reactor&&) = delete;

  /**
   * @brief Process pending I/O events and resume waiting coroutines.
   *
   * @param max_wait Maximum time to wait for events (e.g., std::chrono::milliseconds(100)).
   * @return An expected<void, core::error>. On success, contains void.
   *         On failure, contains an error describing the epoll_wait/kevent failure.
   * @note Must be called from the owning thread.
   */
  [[nodiscard]] expected<void, core::error> process_loop(std::chrono::nanoseconds max_wait);

  /**
   * @brief Create a new TCP or UDP socket.
   *
   * Creates a socket using the POSIX socket() syscall.
   * @param kind The socket kind (stream/TCP or datagram/UDP).
   * @return An expected<socket_type, core::error>. On success, contains the new socket descriptor.
   *         On failure, contains an error describing the creation failure.
   */
  [[nodiscard]] expected<socket_type, core::error> create_socket(socket_type_id kind) noexcept;

  /**
   * @brief Bind a socket to a local address.
   *
   * Binds the socket to the specified sockaddr. The socket must already be created.
   * @param socket_handle The socket descriptor to bind.
   * @param address Buffer containing the local sockaddr structure.
   * @return An expected<void, core::error>. On success, contains void.
   *         On failure, contains an error describing the bind failure.
   */
  [[nodiscard]] expected<void, core::error> bind_socket(socket_type socket_handle, std::span<const std::byte> address) noexcept;

  /**
   * @brief Set a socket to listening mode.
   *
   * Calls listen() on the socket with the specified backlog.
   * @param socket_handle The listening socket descriptor.
   * @param backlog Maximum length of the pending connections queue.
   * @return An expected<void, core::error>. On success, contains void.
   *         On failure, contains an error describing the listen failure.
   */
  [[nodiscard]] expected<void, core::error> listen_socket(socket_type socket_handle, int backlog = SOMAXCONN) noexcept;

  /**
   * @brief Add a socket descriptor to the reactor for polling.
   *
   * @param sock The socket id to add.
   * @return The index of the registered fd, or invalid_index on failure.
   * @note The socket will be set to non-blocking mode if not already.
   * @note On epoll, EPOLLET (edge-triggered) is used. On kqueue, level-triggered.
   */
  expected<size_t, core::error> add_sock(socket_type sock);

  /**
   * @brief Remove a socket descriptor from the reactor.
   *
   * @param sock The socket id to remove.
   * @param index The index returned by add_sock.
   * @note The socket is closed after removal.
   * @note On kqueue, EV_DELETE removes the filter; on epoll, EPOLL_CTL_DEL does the same.
   */
  expected<void, core::error> remove_sock(socket_type sock, size_t index);

  /**
   * @brief Register for write readiness notification.
   *
   * Arms epoll/kqueue for write events. When data can be written, the callback is invoked
   * with connection_state::available_write. The actual send() should be performed
   * by the socket code after receiving this notification.
   * @param socket The socket to monitor.
   * @param callback Continuation called when write is possible or on error.
   */
  void submit_send_socket(epoll_socket& socket, continue_size_callback_t&& callback);

  /**
   * @brief Register for read readiness notification.
   *
   * Arms epoll/kqueue for read events. When data is available, the callback is invoked
   * with connection_state::available_read. The actual recv() should be performed
   * by the socket code after receiving this notification.
   * @param socket The socket to monitor.
   * @param callback Continuation called when read is possible or on error.
   */
  void submit_receive_socket(epoll_socket& socket, continue_size_callback_t&& callback);

  /**
   * @brief Register for accept readiness notification.
   *
   * Arms epoll/kqueue for read events on a listening socket. When a connection is pending,
   * the callback is invoked with connection_state::available_read. The actual accept()
   * should be performed by the listener code after receiving this notification.
   * @param socket The listening socket to monitor.
   * @param callback Continuation called when accept is possible or on error.
   */
  void submit_accept_socket(epoll_socket& socket, continue_socket_callback_t&& callback);

  /**
   * @brief Register for connect completion notification.
   *
   * Arms epoll/kqueue for write events. When the non-blocking connect completes,
   * the callback is invoked with connection_state::available_write (success) or
   * connection_state::closed (error).
   * @param socket The connecting socket to monitor.
   * @param callback Continuation called when connect completes or on error.
   */
  void submit_connect_socket(epoll_socket& socket, continue_void_callback_t&& callback);

  /**
   * @brief Close a socket descriptor.
   *
   * @param socket_handle The socket descriptor to close.
   * @param callback Continuation called after close completes with void or error.
   */
  void submit_close_socket(socket_type socket_handle, continue_void_callback_t&& callback);

 private:
  [[nodiscard]] expected<void, core::error> epoll_ctl_impl(socket_type socket_handle, int action, uint32_t flags, void* user_data) const;

 private:
  /**
   * @brief Async socket send operation (notification only).
   */
  struct op_send_socket {
    continue_size_callback_t callback;
  };

  /**
   * @brief Async socket receive operation (notification only).
   */
  struct op_receive_socket {
    continue_size_callback_t callback;
  };

  /**
   * @brief Async socket accept operation (notification only).
   */
  struct op_accept_socket {
    continue_socket_callback_t callback;
  };

  /**
   * @brief Async socket connect operation (notification only).
   */
  struct op_connect_socket {
    continue_void_callback_t callback;
  };

  /**
   * @brief Default empty operation.
   */
  struct no_op {
    static constexpr bool callback = false;
  };

  /**
   * @brief Variant holding all possible epoll/kqueue operation types.
   */
  using request_variant = std::variant<no_op, op_send_socket, op_receive_socket,
                                       op_accept_socket, op_connect_socket>;

  /**
   * @brief Per-file-descriptor state tracked by the reactor.
   */
  struct fd_state {
    socket_type sock = invalid_socket_id;
    request_variant pending_op;
  };

  async_coro::mutex _mutex;
  std::vector<fd_state> _handled_fds CORO_THREAD_GUARDED_BY(_mutex);
  std::vector<size_t> _empty_fds CORO_THREAD_GUARDED_BY(_mutex);

  file_handle_t _epoll_fd = invalid_file_handle;
};

}  // namespace server::io

#endif  // EPOLL_KQUEUE_ENABLED
