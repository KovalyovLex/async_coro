#pragma once

#include <async_coro/config.h>
#include <server/http1/http_status_code.h>
#include <server/utils/static_string.h>

#include <string>
#include <string_view>
#include <variant>

namespace server::http1 {

struct http_error {
  http_status_code status_code;
  std::variant<std::string, static_string> reason;

  [[nodiscard]] std::string_view get_reason() const noexcept {
    std::string_view res;
    if (const auto* str = std::get_if<std::string>(&reason)) {
      res = *str;
    } else if (const auto* static_str = std::get_if<static_string>(&reason)) {
      res = static_str->str;
    } else {
      ASYNC_CORO_ASSERT(false && "Unsupported variant type.");  // NOLINT(*static-assert*)
    }

    return res;
  }
};

}  // namespace server::http1
