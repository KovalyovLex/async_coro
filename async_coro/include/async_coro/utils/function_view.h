#pragma once

#include <async_coro/config.h>
#include <async_coro/internal/deduce_function_signature.h>
#include <async_coro/internal/is_invocable_by_signature.h>

#include <cstdlib>
#include <memory>
#include <type_traits>
#include <utility>

namespace async_coro::internal {

struct no_init_func_t {};

template <typename T>
struct is_function_pointer : std::false_type {
};

template <typename R, typename... P>
struct is_function_pointer<R (*)(P...)> : std::true_type {
};

template <typename R, typename... P>
struct is_function_pointer<R (&)(P...)> : std::true_type {
};

template <typename R, typename... P>
struct is_function_pointer<R (*)(P...) noexcept> : std::true_type {
};

template <typename R, typename... P>
struct is_function_pointer<R (&)(P...) noexcept> : std::true_type {
};

template <typename T>
inline constexpr bool is_function_pointer_v = is_function_pointer<T>::value;

template <bool IsNoexcept, typename R, typename... TArgs>
class function_view_base {
 protected:
  using call_signature_t = R (*)(void*, TArgs&&...);

  static R default_call(void* /*obj*/, TArgs&&... /*args*/) noexcept {
    ASYNC_CORO_ASSERT(false && "Unable to call empty function_view");  // NOLINT(*static*)

    std::abort();
  }

  constexpr function_view_base() noexcept
      : _object(nullptr),
        _call(&function_view_base::default_call) {}

  template <typename FxT>
  constexpr function_view_base(no_init_func_t /*tag*/, const FxT& callable) noexcept {
    // const correctness of object guaranteed by function_view SFINAE check
    if constexpr (is_function_pointer_v<FxT>) {
      // function pointer
      using non_ref_t = std::conditional_t<std::is_pointer_v<FxT>, FxT, std::add_pointer_t<std::remove_reference_t<FxT>>>;
      _object = const_cast<void*>(reinterpret_cast<const void*>(callable));  // NOLINT(*-cast)
      _call = static_cast<call_signature_t>([](void* ptr, TArgs&&... args) noexcept(IsNoexcept) -> R {
        return (reinterpret_cast<non_ref_t>(ptr))(std::forward<TArgs>(args)...);  // NOLINT(*reinterpret-cast)
      });
    } else {
      // some lambda object
      using callable_t = std::remove_reference_t<FxT>;
      _object = const_cast<void*>(static_cast<const void*>(std::addressof(callable)));  // NOLINT(*const-cast)
      _call = static_cast<call_signature_t>([](void* ptr, TArgs&&... args) noexcept(IsNoexcept) -> R {
        return (*static_cast<callable_t*>(ptr))(std::forward<TArgs>(args)...);
      });
    }
  }

 public:
  void swap(function_view_base& other) noexcept {
    std::swap(_object, other._object);
    std::swap(_call, other._call);
  }

  void clear() noexcept {
    _object = nullptr;
    _call = &function_view_base::default_call;
  }

  R operator()(TArgs... args) const noexcept(IsNoexcept) {
    return _call(_object, std::forward<TArgs>(args)...);
  }

  constexpr explicit operator bool() const noexcept { return _object != nullptr; }

 private:
  void* _object;
  call_signature_t _call;
};

}  // namespace async_coro::internal

namespace async_coro {

template <typename FunctionType>
class function_view;

template <typename R, typename... P>
class function_view<R(P...) const> final : public internal::function_view_base<false, R, P...> {
  using super = internal::function_view_base<false, R, P...>;

  template <typename Fx>
  using is_invocable = internal::is_invocable_by_signature<R(P...) const, Fx>;

 public:
  constexpr function_view() noexcept = default;
  constexpr explicit function_view(std::nullptr_t) noexcept : super() {}
  function_view(const function_view&) noexcept = default;
  function_view(function_view&&) noexcept = default;
  ~function_view() noexcept = default;
  function_view& operator=(const function_view&) noexcept = default;
  function_view& operator=(function_view&&) noexcept = default;

  template <typename FxT>
    requires(!std::is_same_v<std::remove_cvref_t<FxT>, function_view> && is_invocable<std::remove_reference_t<FxT>>::value)
  constexpr function_view(FxT&& callable) noexcept  // NOLINT(*explicit*)
      : super(internal::no_init_func_t{}, std::forward<FxT>(callable)) {}
};

template <typename R, typename... P>
class function_view<R(P...) const noexcept> final : public internal::function_view_base<true, R, P...> {
  using super = internal::function_view_base<true, R, P...>;

  template <typename Fx>
  using is_invocable = internal::is_invocable_by_signature<R(P...) const noexcept, Fx>;

 public:
  constexpr function_view() noexcept = default;
  constexpr explicit function_view(std::nullptr_t) noexcept : super() {}
  function_view(const function_view&) noexcept = default;
  function_view(function_view&&) noexcept = default;
  ~function_view() noexcept = default;
  function_view& operator=(const function_view&) noexcept = default;
  function_view& operator=(function_view&&) noexcept = default;

  template <typename FxT>
    requires(!std::is_same_v<std::remove_cvref_t<FxT>, function_view> && is_invocable<std::remove_reference_t<FxT>>::value)
  constexpr function_view(FxT&& callable) noexcept  // NOLINT(*explicit*)
      : super(internal::no_init_func_t{}, std::forward<FxT>(callable)) {}
};

template <typename R, typename... P>
class function_view<R(P...)> final : public internal::function_view_base<false, R, P...> {
  using super = internal::function_view_base<false, R, P...>;

  template <typename Fx>
  using is_invocable = internal::is_invocable_by_signature<R(P...), Fx>;

 public:
  constexpr function_view() noexcept = default;
  constexpr explicit function_view(std::nullptr_t) noexcept : super() {}
  function_view(const function_view&) noexcept = default;
  function_view(function_view&&) noexcept = default;
  ~function_view() noexcept = default;
  function_view& operator=(const function_view&) noexcept = default;
  function_view& operator=(function_view&&) noexcept = default;

  template <typename FxT>
    requires(!std::is_same_v<std::remove_cvref_t<FxT>, function_view> && is_invocable<std::remove_reference_t<FxT>>::value)
  constexpr function_view(FxT&& callable) noexcept  // NOLINT(*explicit*)
      : super(internal::no_init_func_t{}, std::forward<FxT>(callable)) {}
};

template <typename R, typename... P>
class function_view<R(P...) noexcept> final : public internal::function_view_base<true, R, P...> {
  using Base = internal::function_view_base<true, R, P...>;

  template <typename Fx>
  using is_invocable = internal::is_invocable_by_signature<R(P...) noexcept, Fx>;

 public:
  constexpr function_view() noexcept = default;
  constexpr explicit function_view(std::nullptr_t) noexcept : Base() {}
  function_view(const function_view&) noexcept = default;
  function_view(function_view&&) noexcept = default;
  ~function_view() noexcept = default;
  function_view& operator=(const function_view&) noexcept = default;
  function_view& operator=(function_view&&) noexcept = default;

  template <typename FxT>
    requires(!std::is_same_v<std::remove_cvref_t<FxT>, function_view> && is_invocable<std::remove_reference_t<FxT>>::value)
  constexpr function_view(FxT&& callable) noexcept  // NOLINT(*explicit*)
      : Base(internal::no_init_func_t{}, std::forward<FxT>(callable)) {}
};

template <typename R, class... TArgs>
function_view(R(TArgs...)) -> function_view<R(TArgs...) const>;

template <typename R, class... TArgs>
function_view(R(TArgs...) noexcept) -> function_view<R(TArgs...) const noexcept>;

template <typename Fx>
function_view(Fx) -> function_view<typename internal::deduce_function_signature<Fx>::view_type>;

template <typename Fx>
bool operator==(const function_view<Fx>& func, std::nullptr_t) noexcept {
  return !func;
}

template <typename Fx>
bool operator==(std::nullptr_t, const function_view<Fx>& func) noexcept {
  return !func;
}

template <typename Fx>
bool operator!=(const function_view<Fx>& func, std::nullptr_t) noexcept {
  return static_cast<bool>(func);
}

template <typename Fx>
bool operator!=(std::nullptr_t, const function_view<Fx>& func) noexcept {
  return static_cast<bool>(func);
}

}  // namespace async_coro

namespace std {

template <typename F>
void swap(async_coro::function_view<F>& func1, async_coro::function_view<F>& func2) noexcept {
  func1.swap(func2);
}

}  // namespace std
