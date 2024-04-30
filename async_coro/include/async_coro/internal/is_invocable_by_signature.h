
#include <type_traits>
#include <utility>

namespace async_coro::internal {

template <bool IsNoexcept, class R, class... TArgs>
struct is_invocable_impl {
  template <class Fx>
  static constexpr auto call(Fx&& obj, TArgs&&... args) noexcept(noexcept(obj(static_cast<TArgs&&>(args)...))) -> decltype(obj(static_cast<TArgs&&>(args)...)) {
    return obj(static_cast<TArgs&&>(args)...);
  }

  template <class Fx, class V = void>
  struct test : std::false_type {};

  template <class Fx>
  struct test<Fx, std::void_t<decltype(call(std::declval<Fx>(), std::declval<TArgs>()...))>> : std::bool_constant<
                                                                                                   (!IsNoexcept || noexcept(std::declval<Fx>()(std::declval<TArgs>()...))) && (std::is_void_v<R> ||
                                                                                                                                                                               std::is_convertible_v<decltype(call(std::declval<Fx>(), std::declval<TArgs>()...)), R>)> {};
};

template <class Fx>
struct function_signature {
  static_assert(always_false<Fx>::value, "can't deduce function signature.");
};

template <typename R, typename... TArgs>
struct function_signature<R(TArgs...) noexcept> {
  using return_type = R;
  inline static constexpr bool is_noexcept = true;

  template <class Fx>
  using is_invocable = typename is_invocable_impl<is_noexcept, R, TArgs...>::template test<Fx>;
};

template <typename R, typename... TArgs>
struct function_signature<R(TArgs...)> {
  using return_type = R;
  inline static constexpr bool is_noexcept = false;

  template <class Fx>
  using is_invocable = typename is_invocable_impl<is_noexcept, R, TArgs...>::template test<Fx>;
};

template <class FxSig, class Fx>
struct is_invocable_by_signature {
  inline static constexpr bool value = function_signature<FxSig>::template is_invocable<Fx>::value;
};

}  // namespace async_coro::internal
