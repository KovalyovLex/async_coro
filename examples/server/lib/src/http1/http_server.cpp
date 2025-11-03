#include <server/http1/http_server.h>
#include <server/http1/session.h>

namespace server::http1 {

http_server::http_server() = default;

http_server::http_server(async_coro::i_execution_system::ptr execution_system) noexcept
    : _scheduler(std::move(execution_system)) {}

void http_server::serve(const tcp_server_config& conf, std::optional<ssl_config> ssl_conf) {
  _server.serve(conf, std::move(ssl_conf), [this](auto conn) {
    _scheduler.start_task(
        [ses = session{std::move(conn), _router}]() mutable {
          return ses.run();
        },
        async_coro::execution_queues::worker);
  });
}

}  // namespace server::http1
