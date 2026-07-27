#pragma once

#include <cstddef>
#include <new>

namespace std_fallback {

// some platforms dont have this constants even if they define __cpp_lib_hardware_interference_size == 201703

inline constexpr std::size_t hardware_constructive_interference_size = 64;
inline constexpr std::size_t hardware_destructive_interference_size = 64;

}  // namespace std_fallback

namespace std {

using namespace ::std_fallback;

}
