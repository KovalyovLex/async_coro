#include <async_coro/internal/always_false.h>

#include <type_traits>

namespace async_coro::internal {

template <typename Fx, typename = void>
struct deduce_function_signature {};  // can't deduce signature when &Fx::operator() is missing, inaccessible, or ambiguous

template <typename T>
struct deduce_function_signature_impl {
  static_assert(always_false<T>::value, "Can't deduce signature.");
};

template <typename R, typename T, typename... TArgs>
struct deduce_function_signature_impl<R (T::*)(TArgs...) const> {
  using type = R(TArgs...);
};

template <typename R, typename T, typename... TArgs>
struct deduce_function_signature_impl<R (T::*)(TArgs...)> {
  using type = R(TArgs...);
};

template <typename R, typename... TArgs>
struct deduce_function_signature_impl<R (*)(TArgs...)> {
  using type = R(TArgs...);
};

template <typename R, typename T, typename... TArgs>
struct deduce_function_signature_impl<R (T::*)(TArgs...) const noexcept> {
  using type = R(TArgs...) noexcept;
};

template <typename R, typename T, typename... TArgs>
struct deduce_function_signature_impl<R (T::*)(TArgs...) noexcept> {
  using type = R(TArgs...) noexcept;
};

template <typename R, typename... TArgs>
struct deduce_function_signature_impl<R (*)(TArgs...) noexcept> {
  using type = R(TArgs...) noexcept;
};

template <typename Fx>
struct deduce_function_signature<Fx, std::void_t<decltype(&Fx::operator())>> {
  using type = typename deduce_function_signature_impl<decltype(&Fx::operator())>::type;
};
}  // namespace async_coro::internal
