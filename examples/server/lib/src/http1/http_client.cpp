#include <server/core/i_read_connection.h>
#include <server/core/i_write_connection.h>
#include <server/http1/http_client.h>
#include <server/http1/request.h>
#include <server/utils/ci_string_view.h>
#include <server/utils/static_string.h>

#include <string_view>

namespace server::http1 {

client_request http_client::forward_request(request&& orig) {
  client_request req{std::move(orig)};

  const auto req_str = orig.to_string_view();

  if (const auto* host_header = req.find_header("Host")) {
    req.add_header(static_string{"X-Forwarded-Host"}, static_string{host_header->second});
  }

  return req;
}

auto http_client::send_request(client_request& req, server::core::i_write_connection& write, server::core::i_read_connection& read) -> async_coro::task<expected<client_response, std::string>> {
  using res_t = expected<client_response, std::string>;

  // make sure request has correct version
  req.set_version(_ver);

  auto send_res = co_await req.send(write);
  if (!send_res) {
    co_return res_t{unexpect, std::move(send_res).error()};
  }

  client_response resp;
  auto read_res = co_await resp.read(read);
  if (!read_res) {
    co_return res_t{unexpect, std::string{read_res.error().get_reason()}};
  }

  co_return res_t{std::move(resp)};
}

}  // namespace server::http1
