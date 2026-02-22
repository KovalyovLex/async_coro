#pragma once

#include <async_coro/internal/await_callback.h>
#include <async_coro/thread_safety/analysis.h>
#include <async_coro/thread_safety/mutex.h>
#include <async_coro/utils/unique_function.h>
#include <server/utils/expected.h>

#include <chrono>
#include <cstdint>
#include <vector>

#include "connection_id.h"
#include "socket_config.h"

namespace server::socket_layer {

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

  // should be called only from owning thread
  void process_loop(std::chrono::nanoseconds max_wait);

  // thread safe methods
  size_t add_connection(connection_id conn);
  void close_connection(connection_id conn, size_t index);

  void continue_after_receive_data(connection_id conn, size_t index, continue_callback_t&& clb);
  void continue_after_sent_data(connection_id conn, size_t index, continue_callback_t&& clb);

 private:
  struct handled_connection;

  async_coro::mutex _mutex;
  std::vector<handled_connection> _handled_connections CORO_THREAD_GUARDED_BY(_mutex);
  std::vector<size_t> _empty_connections CORO_THREAD_GUARDED_BY(_mutex);

  epoll_handle_t _epoll_fd{};
  bool _error = false;
};

}  // namespace server::socket_layer
