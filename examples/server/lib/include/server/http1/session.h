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

  [[nodiscard]] bool is_keep_alive() const noexcept { return _keep_alive; }
  void set_keep_alive(bool value) noexcept { _keep_alive = value; }

  [[nodiscard]] auto& get_connection() noexcept { return _conn; }
  [[nodiscard]] const auto& get_connection() const noexcept { return _conn; }

 private:
  server::socket_layer::connection _conn;
  const router* _router;
  bool _keep_alive = true;
};

}  // namespace server::http1
