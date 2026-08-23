#pragma once

#include <server/io/io_config.h>

#if EPOLL_KQUEUE_ENABLED

#include <async_coro/internal/await_callback.h>
#include <async_coro/thread_safety/analysis.h>
#include <async_coro/thread_safety/mutex.h>
#include <async_coro/utils/unique_function.h>
#include <server/io/io_config.h>
#include <server/utils/expected.h>

#include <chrono>
#include <cstdint>
#include <vector>

namespace server::io {

/**
 * @brief Event reactor for sockets only.
 *
 * This reactor provides an interface for polling socket file descriptors using
 * epoll (Linux) or kqueue (macOS). On Windows, use the IOCP-based reactor instead.
 * This reactor does NOT support regular files — epoll/kqueue are designed for
 * network I/O and do not provide meaningful async notifications for regular file operations.
 *
 * For synchronous file I/O, use server::io::sync_file instead.
 * For async file I/O on Windows, use server::io::iocp_reactor.
 *
 * @note The reactor must outlive any socket that is registered with it.
 *       The caller is responsible for ensuring proper lifetime management.
 * @note This class is not copyable or movable to prevent accidental sharing.
 */
class reactor {
 public:
  enum class connection_state : uint8_t {
    available_read,
    available_write,
    closed,
  };

  using continue_callback_t = async_coro::unique_function<void(connection_state), sizeof(async_coro::internal::await_continue_callback<connection_state>)>;

  reactor() noexcept;
  reactor(const reactor&) = delete;
  reactor(reactor&&) = delete;

  ~reactor() noexcept;

  reactor& operator=(const reactor&) = delete;
  reactor& operator=(reactor&&) = delete;

  /**
   * @brief Process pending I/O events and resume waiting coroutines.
   *
   * @param max_wait Maximum time to wait for events (e.g., std::chrono::milliseconds(100)).
   * @return An expected<void, core::error>. On success, contains void.
   *         On failure, contains an error describing the epoll/kqueue failure.
   * @note Must be called from the owning thread.
   */
  [[nodiscard]] expected<void, core::error> process_loop(std::chrono::nanoseconds max_wait);

  /**
   * @brief Add a socket descriptor to the reactor for polling.
   *
   * @param sock The socket id to add.
   * @return The index of the registered fd, or invalid_index on failure.
   * @note The socket will be set to non-blocking mode if not already.
   */
  size_t add_sock(socket_type sock);

  /**
   * @brief Remove a socket descriptor from the reactor.
   *
   * @param sock The socket id to remove.
   * @param index The index returned by add_sock.
   * @note The socket is closed after removal.
   */
  void remove_sock(socket_type sock, size_t index);

  /**
   * @brief Register a callback for when data is available for reading.
   *
   * @param sock The socket id.
   * @param index The index returned by add_sock.
   * @param callback The callback to invoke when data is available.
   */
  void continue_after_receive_data(socket_type sock, size_t index, continue_callback_t&& callback);

  /**
   * @brief Register a callback for when data can be written.
   *
   * @param sock The socket id.
   * @param index The index returned by add_sock.
   * @param callback The callback to invoke when write is possible.
   */
  void continue_after_sent_data(socket_type sock, size_t index, continue_callback_t&& callback);

 private:
  enum class await_type : uint8_t {
    send_data,
    receive_data,
    no_await,
  };
  struct handled_fd {
    continue_callback_t callback;
    socket_type sock = invalid_socket_id;
    await_type await = await_type::no_await;
  };

  // File descriptor support removed — epoll/kqueue do not support regular files.
  // Use server::io::sync_file for synchronous file I/O instead.

  async_coro::mutex _mutex;
  std::vector<handled_fd> _handled_fds CORO_THREAD_GUARDED_BY(_mutex);
  std::vector<size_t> _empty_fds CORO_THREAD_GUARDED_BY(_mutex);

  file_handle_t _epoll_fd = invalid_file_handle;
  bool _error = false;
};

}  // namespace server::io

#endif
