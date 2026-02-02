#include <async_coro/config.h>
#include <server/http1/http_method.h>

#include <optional>
#include <string_view>

namespace server::http1 {

std::string_view as_string(http_method method) noexcept {
  switch (method) {
    case http_method::GET:
      return "GET";
    case http_method::HEAD:
      return "HEAD";
    case http_method::POST:
      return "POST";
    case http_method::PUT:
      return "PUT";
    case http_method::DELETE:
      return "DELETE";
    case http_method::CONNECT:
      return "CONNECT";
    case http_method::OPTIONS:
      return "OPTIONS";
    case http_method::TRACE:
      return "TRACE";
    case http_method::PATCH:
      return "PATCH";
  }
  ASYNC_CORO_ASSERT(false && "Unhandled enum value of http_method");  // NOLINT(*static-assert)
  return "";
}

std::optional<http_method> as_method(std::string_view str) noexcept {
  using namespace std::string_view_literals;

  if (str == "GET"sv) {
    return http_method::GET;
  }
  if (str == "HEAD"sv) {
    return http_method::HEAD;
  }
  if (str == "POST"sv) {
    return http_method::POST;
  }
  if (str == "PUT"sv) {
    return http_method::PUT;
  }
  if (str == "DELETE"sv) {
    return http_method::DELETE;
  }
  if (str == "CONNECT"sv) {
    return http_method::CONNECT;
  }
  if (str == "OPTIONS"sv) {
    return http_method::OPTIONS;
  }
  if (str == "TRACE"sv) {
    return http_method::TRACE;
  }
  if (str == "PATCH"sv) {
    return http_method::PATCH;
  }

  return std::nullopt;
}

}  // namespace server::http1
