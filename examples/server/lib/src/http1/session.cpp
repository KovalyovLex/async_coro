#include <async_coro/await/await_callback.h>
#include <server/http1/request.h>
#include <server/http1/response.h>
#include <server/http1/session.h>
#include <server/socket_layer/connection.h>

#include <memory>

namespace server::http1 {

session::session(server::socket_layer::connection conn, const router& router) noexcept
    : _conn(std::move(conn)),
      _router(std::addressof(router)) {}

[[nodiscard]] async_coro::task<void> session::run() {
  bool keep_alive = true;
  request req;
  while (!_conn.is_closed() && keep_alive) {
    auto read_success = co_await req.read(_conn);
    if (!read_success) {
      if (!_conn.is_closed()) {
        response res{http_version::http_1_1, std::move(read_success).error()};
        co_await res.send(_conn);
      }
      continue;
    }

    if (req.get_version() < http_version::http_1_1) {
      response res{http_version::http_1_1, status_code::HttpVersionNotSupported};
      res.add_header("Connection", "close");
      co_await res.send(_conn);
      _conn.close_connection();
      co_return;
    }

    if (auto* head = req.find_header("Connection")) {
      if (head->second == "close") {
        keep_alive = false;
      }
    }

    if (auto* handler = _router->find_handler(req)) {
      auto res = co_await (*handler)(req);
      co_await res.send(_conn);
    } else {
      response res{http_version::http_1_1, status_code::NotFound};
      co_await res.send(_conn);
    }
  }
}

}  // namespace server::http1
