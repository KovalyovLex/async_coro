#include <async_coro/execution_system.h>
#include <server/http1/http_server.h>
#include <server/http1/session.h>

namespace server::http1 {

http_server::http_server()
    : _scheduler(std::make_unique<async_coro::execution_system>(
          async_coro::execution_system_config{
              .worker_configs = {{"worker1"}},
              .main_thread_allowed_tasks = async_coro::execution_thread_mask{}})) {}

http_server::http_server(async_coro::i_execution_system::ptr execution_system) noexcept
    : _scheduler(std::move(execution_system)) {}

void http_server::serve(const http_server_config& conf, std::optional<ssl_config> ssl_conf, listener_connection_opened_t on_listener_open) {
  session_config session_conf{
      .router_ref = _router,
      .compression = _compression_pool,
      .keep_alive_timeout = conf.keep_alive_timeout,
      .max_requests = conf.max_requests};

  _server.serve(
      conf.tcp_config,
      std::move(ssl_conf),
      [this, &session_conf](auto conn) mutable {
        _scheduler.start_task(
            start_session(std::move(conn), session_conf),
            async_coro::execution_queues::worker);
      },
      on_listener_open);
}

}  // namespace server::http1
