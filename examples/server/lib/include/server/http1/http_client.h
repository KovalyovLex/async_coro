#pragma once

#include <async_coro/task.h>
#include <server/http1/client_request.h>
#include <server/http1/client_response.h>
#include <server/socket_layer/connection.h>
#include <server/utils/expected.h>

namespace server::http1 {

// Thin helper that knows how to perform a request/response exchange over an
// existing `socket_layer::connection`.  It is intentionally minimal: the
// caller manages connection lifetime, timeouts, and retries, which makes it
// suitable for building proxy logic where the same connection object is used
// in both directions.
class http_client {
 public:
  explicit http_client(http_version ver = http_version::http_1_1) noexcept : _ver(ver) {}

  http_client(const http_client&) = delete;
  http_client(http_client&&) = default;
  http_client& operator=(const http_client&) = delete;
  http_client& operator=(http_client&&) = default;

  // Perform the round-trip.  On success the response object is returned
  // (already parsed).  The connection may be reused for another request if
  // the server supports keep-alive.
  async_coro::task<expected<client_response, std::string>>
  send_request(client_request& req, server::socket_layer::connection& conn) {
    using res_t = expected<client_response, std::string>;

    // make sure request has correct version
    req.set_version(_ver);

    auto send_res = co_await req.send(conn);
    if (!send_res) {
      co_return res_t{unexpect, std::move(send_res).error()};
    }

    client_response resp;
    auto read_res = co_await resp.read(conn);
    if (!read_res) {
      co_return res_t{unexpect, std::string{read_res.error().get_reason()}};
    }

    co_return res_t{std::move(resp)};
  }

 private:
  http_version _ver;
};

}  // namespace server::http1
