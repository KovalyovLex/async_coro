#pragma once

#include <async_coro/internal/continue_callback.h>
#include <async_coro/thread_safety/analysis.h>
#include <async_coro/thread_safety/mutex.h>
#include <async_coro/unique_function.h>

#include <chrono>
#include <cstdint>
#include <vector>

#include "connection_id.h"

namespace server::socket_layer {

class reactor {
 public:
  using continue_callback = async_coro::unique_function<void(), sizeof(async_coro::internal::continue_callback)>;

  reactor() noexcept;

  // should be called only from owning thread
  void process_loop(std::chrono::nanoseconds max_wait);

  // thread safe methods
  void add_connection(connection_id conn);
  void close_connection(connection_id conn);

  void continue_after_receive_data(connection_id conn, continue_callback clb);
  void continue_after_sent_data(connection_id conn, continue_callback clb);

 private:
  async_coro::mutex _mutex;
  std::vector<std::pair<connection_id, continue_callback>> _continuations CORO_THREAD_GUARDED_BY(_mutex);
  std::vector<connection_id> _connections_to_close CORO_THREAD_GUARDED_BY(_mutex);

  int64_t _epoll_fid = -1;
  bool _error = false;
};

}  // namespace server::socket_layer
