#include <async_coro/await/await_callback.h>
#include <server/http1/request.h>
#include <server/http1/response.h>
#include <server/http1/router.h>
#include <server/http1/session.h>
#include <server/socket_layer/connection.h>

#include <memory>
#include <type_traits>
#include <variant>

namespace server::http1 {

session::session(server::socket_layer::connection conn, const router& router) noexcept
    : _conn(std::move(conn)),
      _router(std::addressof(router)) {}

[[nodiscard]] async_coro::task<void> session::run() {  // NOLINT(*complexity)
  request req;
  response res{http_version::http_1_1};

  while (!_conn.is_closed() && _keep_alive) {
    auto read_success = co_await req.read(_conn);
    if (!read_success) {
      if (!_conn.is_closed()) {
        res.set_status(std::move(read_success).error());
        co_await res.send(_conn);
      }
      continue;
    }

    if (req.get_version() < http_version::http_1_1) {
      res.set_status(status_code::http_version_not_supported);
      res.add_header(static_string{"Connection"}, static_string{"close"});
      co_await res.send(_conn);
      _conn.close_connection();
      co_return;
    }

    if (const auto* head = req.find_header("Connection")) {
      if (head->second == "close") {
        set_keep_alive(false);
      }
    }

    if (const auto* handler = _router->find_handler(req)) {
      if (const auto* advanced_f = std::get_if<router::advanced_handler_t>(handler)) {
        co_await (*advanced_f)(req, *this);
        continue;
      }

      if (const auto* simple_f = std::get_if<router::simple_handler_t>(handler)) {
        co_await (*simple_f)(req, res);
        if (!res.was_sent()) {
          co_await res.send(_conn);
        }
        res.clear();
        continue;
      }
    }
    res.set_status(status_code::not_found);
    co_await res.send(_conn);
  }
}

}  // namespace server::http1
