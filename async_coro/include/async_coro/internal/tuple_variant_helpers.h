#pragma once

#include <cstddef>
#include <tuple>
#include <type_traits>
#include <variant>

namespace async_coro::internal {

template <class... TArgs>
struct types_holder {
  template <std::size_t I>
  constexpr auto get() const noexcept {
  }
};

// Implementations

template <class... TArgs>
constexpr types_holder<TArgs...> replace_void_type_in_holder_impl(types_holder<TArgs...>, types_holder<>) noexcept {
  return {};
}

template <class T, class... TArgs1, class... TArgs2>
constexpr auto replace_void_type_in_holder_impl(types_holder<TArgs1...>, types_holder<T, TArgs2...>) noexcept {
  if constexpr (std::is_void_v<T>) {
    return replace_void_type_in_holder_impl(types_holder<TArgs1..., std::monostate>{}, types_holder<TArgs2...>{});
  } else {
    return replace_void_type_in_holder_impl(types_holder<TArgs1..., T>{}, types_holder<TArgs2...>{});
  }
}

template <std::size_t... Ints, class TTuple, class TElem>
constexpr auto replace_last_tuple_elem_impl(std::integer_sequence<std::size_t, Ints...>, TTuple&& tuple, TElem&& new_last_elem) noexcept {
  static_assert(!std::is_reference_v<TElem>);

  using tuple_t = std::tuple<typename std::tuple_element<Ints, TTuple>::type..., TElem>;

  return tuple_t{std::get<Ints>(std::move(tuple))..., std::move(new_last_elem)};
}

template <class TTuple, std::size_t... Ints>
constexpr std::integer_sequence<std::size_t, Ints...> get_tuple_index_seq_without_voids_impl(std::integer_sequence<std::size_t, Ints...>, std::integer_sequence<std::size_t>) {
  return {};
}

template <class TTuple, std::size_t I, std::size_t... Ints1, std::size_t... Ints2>
constexpr auto get_tuple_index_seq_without_voids_impl(std::integer_sequence<std::size_t, Ints1...>, std::integer_sequence<std::size_t, I, Ints2...>) {
  if constexpr (std::is_void_v<typename std::tuple_element<I, TTuple>::type>) {
    return get_tuple_index_seq_without_voids_impl<TTuple>(std::integer_sequence<std::size_t, Ints1...>{}, std::integer_sequence<std::size_t, Ints2...>{});
  } else {
    return get_tuple_index_seq_without_voids_impl<TTuple>(std::integer_sequence<std::size_t, Ints1..., I>{}, std::integer_sequence<std::size_t, Ints2...>{});
  }
}

template <class TTuple, std::size_t... Ints>
constexpr std::integer_sequence<std::size_t, Ints...> get_tuple_index_seq_of_voids_impl(std::integer_sequence<std::size_t, Ints...>, std::integer_sequence<std::size_t>) {
  return {};
}

template <class TTuple, std::size_t I, std::size_t... Ints1, std::size_t... Ints2>
constexpr auto get_tuple_index_seq_of_voids_impl(std::integer_sequence<std::size_t, Ints1...>, std::integer_sequence<std::size_t, I, Ints2...>) {
  if constexpr (!std::is_void_v<typename std::tuple_element<I, TTuple>::type>) {
    return get_tuple_index_seq_of_voids_impl<TTuple>(std::integer_sequence<std::size_t, Ints1...>{}, std::integer_sequence<std::size_t, Ints2...>{});
  } else {
    return get_tuple_index_seq_of_voids_impl<TTuple>(std::integer_sequence<std::size_t, Ints1..., I>{}, std::integer_sequence<std::size_t, Ints2...>{});
  }
}

// Implementations end

template <class... TArgs>
constexpr auto replace_void_type_in_holder(types_holder<TArgs...>) noexcept {
  return replace_void_type_in_holder_impl(types_holder<>{}, types_holder<TArgs...>{});
}

template <class... TArgs>
constexpr auto get_variant_for_types(types_holder<TArgs...>) noexcept {
  return std::variant<TArgs...>{};
}

template <class TTuple, class TElem>
constexpr auto replace_last_tuple_elem(TTuple&& tuple, TElem&& new_last_elem) noexcept {
  static_assert(std::tuple_size_v<TTuple> > 1);

  return replace_last_tuple_elem_impl(std::make_index_sequence<std::tuple_size_v<TTuple> - 1>{}, std::forward<TTuple>(tuple), std::forward<TElem>(new_last_elem));
}

template <class TTuple>
constexpr auto get_tuple_index_seq_without_voids() {
  return get_tuple_index_seq_without_voids_impl<TTuple>(std::integer_sequence<std::size_t>{}, std::make_index_sequence<std::tuple_size_v<TTuple>>{});
}

template <class TTuple>
constexpr auto get_tuple_index_seq_of_voids() {
  return get_tuple_index_seq_of_voids_impl<TTuple>(std::integer_sequence<std::size_t>{}, std::make_index_sequence<std::tuple_size_v<TTuple>>{});
}

}  // namespace async_coro::internal
