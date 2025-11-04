#include <server/http1/http_version.h>

#include <optional>
#include <string_view>

namespace server::http1 {

std::string_view as_string(http_version ver) noexcept {
  switch (ver) {
    case http_version::http_0_9:
      return "HTTP/0.9";
    case http_version::http_1_0:
      return "HTTP/1.0";
    case http_version::http_1_1:
      return "HTTP/1.1";
    case http_version::http_2_0:
      return "HTTP/2.0";
  }
  return {};
}

std::optional<http_version> as_http_version(std::string_view str) noexcept {
  using namespace std::string_view_literals;

  if (str == "HTTP/2.0"sv) {
    return http_version::http_2_0;
  }
  if (str == "HTTP/2"sv) {
    return http_version::http_2_0;
  }
  if (str == "HTTP/1.1"sv) {
    return http_version::http_1_1;
  }
  if (str == "HTTP/1.0"sv) {
    return http_version::http_1_0;
  }
  if (str == "HTTP/0.9"sv) {
    return http_version::http_0_9;
  }

  return std::nullopt;
}

}  // namespace server::http1
