#include <server/core/i_read_connection.h>
#include <server/core/i_write_connection.h>
#include <server/http1/forwarding_params.h>
#include <server/http1/http_client.h>
#include <server/http1/request.h>
#include <server/utils/ci_string_view.h>
#include <server/utils/static_string.h>

#include <string_view>

namespace server::http1 {

client_request http_client::forward_request(request&& orig, const forwarding_params& params) {
  client_request req{std::move(orig)};

  // Remove X-Forwarded-* headers completely (not RFC 7239 compliant)
  req.remove_headers([](const auto& pair) {
    constexpr auto kXForwarded = "X-Forwarded"_ci_sv;

    return pair.first.substr(0, kXForwarded.size()) == kXForwarded;
  });

  // Get existing Forwarded header if present
  std::string_view existing_value;
  std::string existing_str;

  // Remove an old header and store its value. We can safely keep string_view as it will kept in storage
  req.remove_headers([&](const auto& pair) {
    if (pair.first == "Forwarded"_ci_sv) {
      // Concatenate different Fowarded headers
      if (!existing_value.empty() && existing_str.empty()) {
        existing_str = existing_value;
      }

      if (!existing_str.empty()) {
        existing_str += ", ";
        existing_str += existing_value;
        existing_value = existing_str;
      } else {
        existing_value = pair.second;
      }

      return true;
    }
    return false;
  });

  // Format the new proxy element using RFC 7239 parameters
  auto new_element = params.format_proxy_element(existing_value);

  req.add_header(static_string{"Forwarded"}, std::move(new_element));

  return req;
}

auto http_client::send_request(client_request& req, server::core::i_write_connection& write, server::core::i_read_connection& read) -> async_coro::task<expected<client_response, std::string>> {  // NOLINT(cppcoreguidelines-avoid-reference-coroutine-parameters): request lifetime managed by caller through session
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
