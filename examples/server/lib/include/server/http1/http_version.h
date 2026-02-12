#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace server::http1 {

enum class http_version : uint8_t {
  http_0_9 = 9,
  http_1_0 = 10,
  http_1_1 = 11,
  http_2_0 = 20,
};

std::string_view as_string(http_version ver) noexcept;

std::optional<http_version> as_http_version(std::string_view str) noexcept;

}  // namespace server::http1
