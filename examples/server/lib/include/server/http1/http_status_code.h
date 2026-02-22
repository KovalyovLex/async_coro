#pragma once

#include <cstdint>
#include <string_view>

namespace server::http1 {

// Some of the error codes listed in https://developer.mozilla.org/en-US/docs/Web/HTTP/Status
enum class status_code : uint16_t {
  switching_protocols = 101,
  ok = 200,
  created = 201,
  accepted = 202,
  no_content = 204,
  partial_content = 206,
  multiple_choices = 300,
  moved_permanently = 301,
  found = 302,
  not_modified = 304,
  temporary_redirect = 307,
  bad_request = 400,
  unauthorized = 401,
  forbidden = 403,
  not_found = 404,
  method_not_allowed = 405,
  request_timeout = 408,
  length_required = 411,
  upgrade_required = 426,
  internal_server_error = 500,
  not_implemented = 501,
  bad_gateway = 502,
  service_unavailable = 503,
  gateway_timeout = 504,
  http_version_not_supported = 505
};

struct http_status_code {
  uint16_t value;

  explicit http_status_code(uint16_t val) noexcept : value(val) {}

  http_status_code(status_code val) noexcept : value(static_cast<uint16_t>(val)) {}  // NOLINT(*explicit*)

  auto operator<=>(const http_status_code&) const noexcept = default;
  auto operator<=>(uint16_t val) const noexcept {
    return value <=> val;
  }
  auto operator<=>(status_code val) const noexcept {
    return value <=> static_cast<uint16_t>(val);
  }
};

std::string_view as_string(status_code met) noexcept;

}  // namespace server::http1
