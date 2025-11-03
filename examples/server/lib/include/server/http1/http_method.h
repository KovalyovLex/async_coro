#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace server::http1 {

enum class http_method : uint8_t {
  GET,
  HEAD,
  POST,
  PUT,
  DELETE,
  CONNECT,
  OPTIONS,
  TRACE,
  PATCH
};

std::string_view as_string(http_method met) noexcept;

std::optional<http_method> as_method(std::string_view str) noexcept;

}  // namespace server::http1
