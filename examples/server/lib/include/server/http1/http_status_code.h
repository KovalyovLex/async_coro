#pragma once

#include <cstdint>
#include <string_view>

namespace server::http1 {

// Some of the error codes listed in https://developer.mozilla.org/en-US/docs/Web/HTTP/Status
enum class status_code : uint16_t {
  SwitchingProtocols = 101,
  Ok = 200,
  Created = 201,
  Accepted = 202,
  NoContent = 204,
  PartialContent = 206,
  MultipleChoices = 300,
  MovedPermanently = 301,
  Found = 302,
  NotModified = 304,
  TemporaryRedirect = 307,
  BadRequest = 400,
  Unauthorized = 401,
  Forbidden = 403,
  NotFound = 404,
  MethodNotAllowed = 405,
  RequestTimeout = 408,
  LengthRequired = 411,
  UpgradeRequired = 426,
  InternalServerError = 500,
  NotImplemented = 501,
  BadGateway = 502,
  ServiceUnavailable = 503,
  GatewayTimeout = 504,
  HttpVersionNotSupported = 505
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
