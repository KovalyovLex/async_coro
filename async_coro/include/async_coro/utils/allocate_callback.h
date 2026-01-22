#pragma once

#include <async_coro/internal/deduce_function_signature.h>
#include <async_coro/utils/callback_ptr.h>

namespace async_coro {

/**
 * @brief Allocates a new concrete callback that wraps a given callable.
 *
 * This function creates a small, derived object that stores the callable `fx`
 * and provides the necessary `executor` and `deleter` function pointers for
 * the type-erased `callback` base class.
 *
 * @tparam Fx The type of the callable to wrap.
 * @param fx The callable object to be wrapped.
 * @return A std::unique_ptr to the newly allocated callback.
 */
template <class Fx>
auto allocate_callback(Fx&& func) {
  using callback_type = typename internal::deduce_function_signature<std::remove_cvref_t<Fx>>::callback_type;
  using ptr = callback_ptr<typename callback_type::callback_signature>;

  return ptr::allocate(std::forward<Fx>(func));
}

}  // namespace async_coro
