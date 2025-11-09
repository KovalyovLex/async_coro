#pragma once

#include <async_coro/task.h>
#include <server/http1/request.h>
#include <server/socket_layer/connection.h>

namespace server::web_socket {

class web_socket_session {
 public:
  explicit web_socket_session(server::socket_layer::connection conn) noexcept
      : _conn(std::move(conn)) {}

  // Runs request->response loop until connection closed or handler signals close.
  [[nodiscard]] async_coro::task<void> run(const server::http1::request& handshake_request);

 private:
  server::socket_layer::connection _conn;
};

}  // namespace server::web_socket
