#pragma once

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
 * @brief Common event reactor for both sockets and files.
 *
 * This reactor provides a unified interface for polling file descriptors using
 * epoll (Linux) or kqueue (macOS). It supports both socket and regular file
 * descriptors, with appropriate event filtering for each type.
 *
 * @note The reactor must outlive any file or socket that is registered with it.
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
   * @note Must be called from the owning thread.
   */
  void process_loop(std::chrono::nanoseconds max_wait);

  /**
   * @brief Add a file descriptor to the reactor for polling.
   *
   * @param file_descriptor The file descriptor to add (regular file).
   * @return The index of the registered fd, or invalid_index on failure.
   * @note The fd will be set to non-blocking mode if not already.
   */
  size_t add_fd(file_handle_t file_descriptor);

  /**
   * @brief Remove a file descriptor from the reactor.
   *
   * @param file_descriptor The file descriptor to remove.
   * @param index The index returned by add_fd.
   * @note The fd is closed after removal.
   */
  void remove_fd(file_handle_t file_descriptor, size_t index);

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
   * @param file_descriptor The file descriptor.
   * @param index The index returned by add_fd.
   * @param callback The callback to invoke when data is available.
   */
  void continue_after_read_data_ready(file_handle_t file_descriptor, size_t index, continue_callback_t&& callback);

  /**
   * @brief Register a callback for when data can be written.
   *
   * @param file_descriptor The file descriptor.
   * @param index The index returned by add_fd.
   * @param callback The callback to invoke when write is possible.
   */
  void continue_after_write_data(file_handle_t file_descriptor, size_t index, continue_callback_t&& callback);

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

  async_coro::mutex _mutex;
  std::vector<handled_fd> _handled_fds CORO_THREAD_GUARDED_BY(_mutex);
  std::vector<size_t> _empty_fds CORO_THREAD_GUARDED_BY(_mutex);

#if EPOLL_SOCKET || KQUEUE_SOCKET
  epoll_handle_t _epoll_fd = invalid_epoll_handle;
#endif
  bool _error = false;
};

}  // namespace server::io
