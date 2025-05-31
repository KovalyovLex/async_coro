#pragma once

#include <cstdint>

namespace async_coro::internal {

template <class T>
constexpr inline T get_mask(std::uint32_t num_bits) {
  if (num_bits == 0) {
    return T{0};
  }
  return (T{1} << (num_bits - 1)) | get_mask<T>(num_bits - 1);
}

template <class T>
constexpr inline T get_num_bits(std::uint32_t pow_of_two, std::uint32_t cur_num = 0) {
  const auto value = T{1} << cur_num;
  if (pow_of_two == value) {
    return cur_num;
  }
  return get_num_bits<T>(pow_of_two, cur_num + 1);
}

template <class T>
struct tagged_pair {
  T* ptr;
  std::uint32_t tag;
};

}  // namespace async_coro::internal
