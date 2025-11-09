#pragma once

#include <async_coro/task.h>
#include <server/http1/router.h>
#include <server/socket_layer/connection.h>

namespace server::http1 {

class session {
 public:
  session(server::socket_layer::connection conn, const router& router) noexcept;
  session(const session&) = delete;
  session(session&&) noexcept = default;

  ~session() noexcept = default;

  session& operator=(const session&) = delete;
  session& operator=(session&&) noexcept = default;

  // Runs request->response loop until connection closed or handler signals close.
  [[nodiscard]] async_coro::task<void> run();

 private:
  server::socket_layer::connection _conn;
  const router* _router;
};

}  // namespace server::http1
