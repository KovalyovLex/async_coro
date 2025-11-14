#pragma once

#include <type_traits>

namespace async_coro {

template <typename... T>
struct always_false : std::false_type {};

}  // namespace async_coro
