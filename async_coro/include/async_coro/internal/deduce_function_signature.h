#pragma once

#include <async_coro/utils/always_false.h>

#include <type_traits>

namespace async_coro {

template <class R>
class callback;

namespace internal {

template <typename Fx, typename = void>
struct deduce_function_signature {};  // can't deduce signature when &Fx::operator() is missing, inaccessible, or ambiguous

template <typename T>
struct deduce_function_signature_impl {
  static_assert(always_false<T>::value, "Can't deduce signature.");
};

template <typename R, typename T, typename... TArgs>
struct deduce_function_signature_impl<R (T::*)(TArgs...) const> {
  using type = R(TArgs...);
  using view_type = R(TArgs...) const;
  using callback_type = callback<R(TArgs...)>;
};

template <typename R, typename T, typename... TArgs>
struct deduce_function_signature_impl<R (T::*)(TArgs...)> {
  using type = R(TArgs...);
  using const_correct_type = R(TArgs...);
  using callback_type = callback<R(TArgs...)>;
};

template <typename R, typename... TArgs>
struct deduce_function_signature_impl<R (*)(TArgs...)> {
  using type = R(TArgs...);
  using const_correct_type = R(TArgs...) const;  // We make this signature as const to be able to call operator() with constant object as this is free function
  using callback_type = callback<R(TArgs...)>;
};

template <typename R, typename T, typename... TArgs>
struct deduce_function_signature_impl<R (T::*)(TArgs...) const noexcept> {
  using type = R(TArgs...) noexcept;
  using const_correct_type = R(TArgs...) const noexcept;
  using callback_type = callback<R(TArgs...) noexcept>;
};

template <typename R, typename T, typename... TArgs>
struct deduce_function_signature_impl<R (T::*)(TArgs...) noexcept> {
  using type = R(TArgs...) noexcept;
  using const_correct_type = R(TArgs...) noexcept;
  using callback_type = callback<R(TArgs...) noexcept>;
};

template <typename R, typename... TArgs>
struct deduce_function_signature_impl<R (*)(TArgs...) noexcept> {
  using type = R(TArgs...) noexcept;
  using const_correct_type = R(TArgs...) const noexcept;  // We make this signature as const to be able to call operator() with constant object as this is free function
  using callback_type = callback<R(TArgs...) noexcept>;
};

template <typename Fx>
struct deduce_function_signature<Fx, std::void_t<decltype(&Fx::operator())>> : deduce_function_signature_impl<decltype(&Fx::operator())> {
};

}  // namespace internal

}  // namespace async_coro
