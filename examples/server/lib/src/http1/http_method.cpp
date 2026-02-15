#include <async_coro/config.h>
#include <server/http1/http_method.h>

#include <optional>
#include <string_view>

namespace server::http1 {

std::string_view as_string(http_method method) noexcept {
  switch (method) {
    case http_method::Get:
      return "GET";
    case http_method::Head:
      return "HEAD";
    case http_method::Post:
      return "POST";
    case http_method::Put:
      return "PUT";
    case http_method::Delete:
      return "DELETE";
    case http_method::Connect:
      return "CONNECT";
    case http_method::Options:
      return "OPTIONS";
    case http_method::Trace:
      return "TRACE";
    case http_method::Patch:
      return "PATCH";
  }
  ASYNC_CORO_ASSERT(false && "Unhandled enum value of http_method");  // NOLINT(*static-assert)
  return "";
}

std::optional<http_method> as_method(std::string_view str) noexcept {
  using namespace std::string_view_literals;

  if (str == "GET"sv) {
    return http_method::Get;
  }
  if (str == "HEAD"sv) {
    return http_method::Head;
  }
  if (str == "POST"sv) {
    return http_method::Post;
  }
  if (str == "PUT"sv) {
    return http_method::Put;
  }
  if (str == "DELETE"sv) {
    return http_method::Delete;
  }
  if (str == "CONNECT"sv) {
    return http_method::Connect;
  }
  if (str == "OPTIONS"sv) {
    return http_method::Options;
  }
  if (str == "TRACE"sv) {
    return http_method::Trace;
  }
  if (str == "PATCH"sv) {
    return http_method::Patch;
  }

  return std::nullopt;
}

}  // namespace server::http1
