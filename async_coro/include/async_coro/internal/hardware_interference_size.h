#pragma once

namespace std_fallback {
inline constexpr size_t hardware_constructive_interference_size = 64;
inline constexpr size_t hardware_destructive_interference_size = 64;
}  // namespace std_fallback

namespace std {
using namespace ::std_fallback;
}