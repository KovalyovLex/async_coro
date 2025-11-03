#pragma once

#include <async_coro/scheduler.h>
#include <server/http1/router.h>
#include <server/tcp_server.h>

namespace server::http1 {

class http_server {
 public:
  http_server();
  explicit http_server(async_coro::i_execution_system::ptr execution_system) noexcept;

  http_server(const http_server&) = delete;
  http_server(http_server&&) = delete;
  ~http_server() = default;

  http_server& operator=(const http_server&) = delete;
  http_server& operator=(http_server&&) = delete;

  auto& get_router() { return _router; }
  auto& get_scheduler() { return _scheduler; }

  void serve(const tcp_server_config& conf, std::optional<ssl_config> ssl_conf);

  void terminate() { _server.terminate(); }

 private:
  tcp_server _server;
  async_coro::scheduler _scheduler;
  router _router;
};

}  // namespace server::http1
