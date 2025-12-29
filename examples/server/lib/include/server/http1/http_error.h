#pragma once

#include <server/http1/http_status_code.h>
#include <server/utils/static_string.h>

#include <string>
#include <string_view>
#include <type_traits>
#include <variant>

namespace server::http1 {

struct http_error {
  http_status_code status_code;
  std::variant<std::string, static_string> reason;

  [[nodiscard]] std::string_view get_reason() const noexcept {
    std::string_view res;
    std::visit(
        [&res](const auto& val) noexcept {
          using T = std::remove_cvref_t<decltype(val)>;
          if constexpr (std::is_same_v<T, static_string>) {
            res = val.str;
          } else {
            res = val;
          }
        },
        reason);

    return res;
  }
};

}  // namespace server::http1
