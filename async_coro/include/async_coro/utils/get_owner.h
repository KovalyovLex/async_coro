#pragma once

#include <cstddef>
#include <memory>
#include <type_traits>

namespace async_coro {

// Get pointer to owning class by pointer to member and member reference
// member_ptr shouldn't point on any virtual class fields. Works only with pod owner classes
template <class TMember, class TOwner>
TOwner& get_owner(TMember& member, TMember TOwner::* member_ptr) noexcept {
  const auto offset = reinterpret_cast<std::ptrdiff_t>(&(static_cast<TOwner*>(nullptr)->*member_ptr));  // NOLINT(*reinterpret-cast*)

  return *reinterpret_cast<TOwner*>(reinterpret_cast<char*>(std::addressof(member)) - offset);  // NOLINT(*reinterpret-cast*, *pointer*)
}

// Get pointer to owning tuple by pointer to tuple member and index
template <class TTuple, size_t I, class TValue>
TTuple& get_owner_tuple(TValue& member) noexcept {
  static_assert(std::is_same_v<std::remove_cvref_t<decltype(std::get<I>(std::declval<TTuple>()))>, std::remove_cvref_t<TValue>>, "Wrong type provided");
  static_assert(std::is_reference_v<decltype(std::get<I>(std::declval<TTuple>()))>, "Wrong type of tuple?");

  const auto offset = reinterpret_cast<std::ptrdiff_t>(&std::get<I>(*static_cast<TTuple*>(nullptr)));  // NOLINT(*reinterpret-cast*)

  return *reinterpret_cast<TTuple*>(reinterpret_cast<char*>(std::addressof(member)) - offset);  // NOLINT(*reinterpret-cast*, *pointer*)
}

}  // namespace async_coro
