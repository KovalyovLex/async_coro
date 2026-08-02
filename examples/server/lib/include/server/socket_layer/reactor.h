#pragma once

#include <server/io/io_config.h>

#if EPOLL_KQUEUE_ENABLED

#include <server/io/reactor.h>
#include <server/socket_layer/connection_id.h>

#include <cstddef>

namespace server::socket_layer {

/**
 * @brief Socket-specific reactor that wraps the common io::reactor.
 *
 * This reactor delegates all I/O polling to the common io::reactor, providing
 * a socket-specific interface with connection_id abstraction.
 *
 * @note The reactor must outlive any socket that is registered with it.
 *       The caller is responsible for ensuring proper lifetime management.
 */
class reactor {
 public:
  using connection_state = io::reactor::connection_state;
  using continue_callback_t = io::reactor::continue_callback_t;

  reactor() noexcept;
  reactor(const reactor&) = delete;
  reactor(reactor&&) = delete;

  ~reactor() noexcept;

  reactor& operator=(const reactor&) = delete;
  reactor& operator=(reactor&&) = delete;

  /**
   * @brief Process pending I/O events and resume waiting coroutines.
   *
   * @param max_wait Maximum time to wait for events.
   * @note Must be called from the owning thread.
   */
  void process_loop(std::chrono::nanoseconds max_wait);

  /**
   * @brief Add a socket connection to the reactor for polling.
   *
   * @param conn The connection ID (socket fd).
   * @return The index of the registered connection.
   */
  size_t add_connection(connection_id conn);

  /**
   * @brief Close and remove a socket connection from the reactor.
   *
   * @param conn The connection ID to close.
   * @param index The index returned by add_connection.
   */
  void close_connection(connection_id conn, size_t index);

  /**
   * @brief Register a callback for when data is available for reading.
   *
   * @param conn The connection ID.
   * @param index The index returned by add_connection.
   * @param callback The callback to invoke when data is available.
   */
  void continue_after_receive_data(connection_id conn, size_t index, continue_callback_t&& callback);

  /**
   * @brief Register a callback for when data can be written.
   *
   * @param conn The connection ID.
   * @param index The index returned by add_connection.
   * @param callback The callback to invoke when write is possible.
   */
  void continue_after_sent_data(connection_id conn, size_t index, continue_callback_t&& callback);

 private:
  io::reactor _reactor;
};

}  // namespace server::socket_layer

#endif  // EPOLL_KQUEUE_ENABLED
