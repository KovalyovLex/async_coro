#include <server/http1/headers_holder.h>
#include <server/utils/ci_string_view.h>

#include <string_view>

namespace server::http1 {

const core::headers_type::value_type* headers_holder::find_header(std::string_view name) const noexcept {
  const auto ci_name = traits_cast<ascii_ci_traits>(name);

  for (auto&& pair : _headers) {
    if (pair.first != ci_name) {
      continue;
    }
    return std::addressof(pair);
  }

  return nullptr;
}

void headers_holder::foreach_header_with_name(std::string_view name, async_coro::function_view<void(std::string_view)> func) const {
  if (!func) [[unlikely]] {
    return;
  }

  const auto ci_name = traits_cast<ascii_ci_traits>(name);

  for (auto&& pair : _headers) {
    if (pair.first != ci_name) {
      continue;
    }
    func(pair.second);
  }
}

void headers_holder::foreach_header_with_name_noexcept(std::string_view name, async_coro::function_view<void(std::string_view) noexcept> func) const noexcept {
  if (!func) [[unlikely]] {
    return;
  }

  const auto ci_name = traits_cast<ascii_ci_traits>(name);

  for (auto&& pair : _headers) {
    if (pair.first != ci_name) {
      continue;
    }
    func(pair.second);
  }
}

bool headers_holder::has_value_in_header(std::string_view name, std::string_view value) const noexcept {  // NOLINT(*swap*)
  bool has_value = false;

  foreach_header_with_name_noexcept(name, [&](std::string_view head_value) noexcept {
    if (has_value) {
      return;
    }

    const auto idx = head_value.find(value);
    if (idx != std::string_view::npos) {
      if (idx > 0) {
        // check begin
        const auto symbol = head_value[idx - 1];
        if (symbol != ' ' && symbol != ',') {
          // its a substring
          return;
        }
      }
      if (idx + value.size() < head_value.size()) {
        // check end
        const auto symbol = head_value[idx + value.size()];
        if (symbol != ' ' && symbol != ',') {
          // its a substring
          return;
        }
      }

      has_value = true;
    }
  });

  return has_value;
}

}  // namespace server::http1
