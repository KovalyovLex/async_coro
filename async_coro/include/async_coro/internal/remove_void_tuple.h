#pragma once

#include <tuple>
#include <type_traits>

namespace async_coro::internal {

template <typename... Ts>
using tuple_cat_t = decltype(std::tuple_cat(std::declval<Ts>()...));

template <typename... Ts>
using remove_void_tuple_t = tuple_cat_t<
    typename std::conditional<
        std::is_void<Ts>::value,
        std::tuple<>,
        std::tuple<Ts>>::type...>;

}  // namespace async_coro::internal
