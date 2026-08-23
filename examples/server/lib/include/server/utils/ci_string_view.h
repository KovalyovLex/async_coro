#pragma once

#include <cctype>
#include <string_view>

namespace server {

struct ascii_ci_traits : public std::char_traits<char> {
  constexpr static char to_upper(char val) noexcept {
    if (val >= 'a' && val <= 'z') {
      return char(val + 'A');
    }
    return val;
  }

  constexpr static bool eq(char val1, char val2) noexcept {
    return to_upper(val1) == to_upper(val2);
  }

  constexpr static bool lt(char val1, char val2) noexcept {
    return to_upper(val1) < to_upper(val2);
  }

  constexpr static int compare(const char* str1, const char* str2, std::size_t n) noexcept {
    while (n-- != 0) {
      const auto val1 = to_upper(*str1);
      const auto val2 = to_upper(*str2);
      if (val1 < val2) {
        return -1;
      }
      if (val1 > val2) {
        return 1;
      }
      ++str1;  // NOLINT(*pointer*)
      ++str2;  // NOLINT(*pointer*)
    }
    return 0;
  }

  constexpr static const char* find(const char* str, std::size_t n, char val) noexcept {  // NOLINT(*swappable*)
    const auto upper_val{to_upper(val)};
    while (n-- != 0) {
      if (to_upper(*str) == upper_val) {
        return str;
      }
      str++;  // NOLINT(*pointer*)
    }
    return nullptr;
  }
};

using ci_string_view = std::basic_string_view<char, ascii_ci_traits>;

template <class DstTraits, class CharT, class SrcTraits>
constexpr std::basic_string_view<CharT, DstTraits> traits_cast(const std::basic_string_view<CharT, SrcTraits> src) noexcept {
  return {src.data(), src.size()};
}

constexpr ci_string_view operator""_ci_sv(const char* str, std::size_t len) noexcept {
  return {str, len};
}

}  // namespace server
