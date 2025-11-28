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

void http_server::serve(const tcp_server_config& conf, std::optional<ssl_config> ssl_conf, listener_connection_opened_t on_listener_open) {
  _server.serve(
      conf,
      std::move(ssl_conf),
      [this](auto conn) { _scheduler.start_task(
                              [ses = session{std::move(conn), _router, _compression_pool}]() mutable {
                                return ses.run();
                              },
                              async_coro::execution_queues::worker); },
      on_listener_open);
}

}  // namespace server::http1
