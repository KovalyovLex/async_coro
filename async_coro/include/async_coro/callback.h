#pragma once

#include <async_coro/config.h>
#include <async_coro/internal/deduce_function_signature.h>
#include <async_coro/internal/type_traits.h>

#include <memory>
#include <type_traits>
#include <utility>

namespace async_coro {
namespace internal {
template <typename Fx, typename R, typename... TArgs>
struct callable_impl;

template <typename Fx, typename R, typename... TArgs>
struct callable_impl_noexcept;
}  // namespace internal

/**
 * @brief Base class for type-erased callbacks.
 * This class uses a pointer to a deleter function to allow for custom cleanup
 * logic, which is useful for concrete implementations that might be allocated
 * in different ways.
 */
class callback_base {
 public:
  /**
   * @brief A custom deleter for std::unique_ptr to correctly destroy a
   * callback_base object.
   */
  struct deleter {
    void operator()(callback_base* ptr) const noexcept {
      ptr->destroy();
    }
  };

  /**
   * @brief A unique pointer to a callback_base that uses the custom deleter.
   */
  using ptr = std::unique_ptr<callback_base, deleter>;

  /**
   * @brief A function pointer type for a custom deleter.
   */
  using deleter_t = void (*)(callback_base*) noexcept;

  /**
   * @brief Constructs a new callback base object.
   * @param deleter A custom deleter function. If nullptr, the default `delete
   * this` is used.
   */
  callback_base(deleter_t deleter = nullptr) noexcept : _deleter(deleter) {}
  callback_base(const callback_base&) noexcept = default;
  callback_base(callback_base&&) noexcept = default;

  /**
   * @brief Destroys the callback object using either the custom deleter or the
   * default delete operator.
   */
  void destroy() noexcept;

 protected:
  ~callback_base() noexcept = default;

  static void stack_deleter(callback_base*) noexcept;

 private:
  deleter_t _deleter;
};

/**
 * @brief A type-erased callback that takes arguments TArgs... and returns R.
 * @tparam R The return type of the callback.
 * @tparam TArgs The argument types of the callback.
 */
template <typename R, typename... TArgs>
struct callback : public callback_base {
 public:
  /**
   * @brief A unique pointer to a callback that uses the custom deleter.
   */
  using ptr = std::unique_ptr<callback<R, TArgs...>, callback_base::deleter>;

  /**
   * @brief A function pointer type for the callback's executor function.
   */
  using executor_t = R (*)(callback_base*, TArgs...);

  /**
   * @brief Constructs a new callback object.
   * @param executor The function that will be executed when the callback is
   * invoked.
   * @param deleter A custom deleter function.
   */
  callback(executor_t executor, deleter_t deleter = nullptr) noexcept
      : callback_base(deleter),
        _executor(executor) {
    ASYNC_CORO_ASSERT(executor);
  }

  /**
   * @brief Executes the callback with the given arguments.
   * @param value The arguments to pass to the callback.
   */
  R execute(TArgs... value) {
    return _executor(this, std::forward<TArgs>(value)...);
  }

  /**
   * @brief Allocates a new concrete callback that wraps a given callable.
   *
   * This function creates a small, derived object that stores the callable `fx`
   * and provides the necessary `executor` and `deleter` function pointers for
   * the type-erased `callback` base class.
   *
   * @tparam Fx The type of the callable to wrap.
   * @param fx The callable object to be wrapped.
   * @return A pointer to the newly allocated callback. The caller is
   * responsible for managing the memory.
   */
  template <class Fx>
    requires(std::is_invocable_v<Fx, TArgs...> && std::is_same_v<R, std::invoke_result_t<Fx, TArgs...>>)
  static callback* allocate(Fx&& fx) {
    return new internal::callable_impl<std::remove_cvref_t<Fx>, R, TArgs...>(std::forward<Fx>(fx));
  }

 protected:
  ~callback() noexcept = default;

 private:
  executor_t _executor;
};

/**
 * @brief A type-erased callback that has noexcept execute function which takes arguments TArgs... and returns R.
 * @tparam R The return type of the callback.
 * @tparam TArgs The argument types of the callback.
 */
template <typename R, typename... TArgs>
struct callback_noexcept : public callback_base {
 public:
  /**
   * @brief A unique pointer to a callback that uses the custom deleter.
   */
  using ptr = std::unique_ptr<callback_noexcept<R, TArgs...>, callback_base::deleter>;

  /**
   * @brief A function pointer type for the callback's executor function.
   */
  using executor_t = R (*)(callback_base*, TArgs...) noexcept;

  /**
   * @brief Constructs a new callback object.
   * @param executor The function that will be executed when the callback is
   * invoked.
   * @param deleter A custom deleter function.
   */
  callback_noexcept(executor_t executor, deleter_t deleter = nullptr) noexcept
      : callback_base(deleter),
        _executor(executor) {
    ASYNC_CORO_ASSERT(executor);
  }

  /**
   * @brief Executes the callback with the given arguments.
   * @param value The arguments to pass to the callback.
   */
  R execute(TArgs... value) noexcept {
    return _executor(this, std::forward<TArgs>(value)...);
  }

  /**
   * @brief Allocates a new concrete callback that wraps a given callable.
   *
   * This function creates a small, derived object that stores the callable `fx`
   * and provides the necessary `executor` and `deleter` function pointers for
   * the type-erased `callback` base class.
   *
   * @tparam Fx The type of the callable to wrap.
   * @param fx The callable object to be wrapped.
   * @return A pointer to the newly allocated callback. The caller is
   * responsible for managing the memory.
   */
  template <class Fx>
    requires(std::is_nothrow_invocable_v<Fx, TArgs...> && std::is_same_v<R, std::invoke_result_t<Fx, TArgs...>>)
  static callback_noexcept* allocate(Fx&& fx) {
    return new internal::callable_impl_noexcept<std::remove_cvref_t<Fx>, R, TArgs...>(std::forward<Fx>(fx));
  }

 protected:
  ~callback_noexcept() noexcept = default;

 private:
  executor_t _executor;
};

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
auto allocate_callback(Fx&& fx) {
  using callback_type = typename internal::deduce_function_signature<std::remove_cvref_t<Fx>>::callback_type;

  return typename callback_type::ptr{callback_type::allocate(std::forward<Fx>(fx))};
}

namespace internal {

template <typename Fx, typename R, typename... TArgs>
struct callable_impl : public callback<R, TArgs...> {
  template <class T>
  callable_impl(T&& fx) noexcept(std::is_nothrow_constructible_v<Fx, T&&>)
      : callback<R, TArgs...>(&executor, get_deleter()),
        _fx(std::forward<T>(fx)) {}

 protected:
  ~callable_impl() noexcept = default;

 private:
  static callback_base::deleter_t get_deleter() noexcept {
    if constexpr (std::is_trivially_destructible_v<Fx>) {
      return nullptr;
    } else {
      return +[](callback_base* base) noexcept {
        delete static_cast<callable_impl*>(base);
      };
    }
  }

  static R executor(callback_base* base, TArgs... value) {
    return static_cast<callable_impl*>(base)->_fx(std::forward<TArgs>(value)...);
  }

 private:
  Fx _fx;
};

template <typename Fx, typename R, typename... TArgs>
struct concrete_callable_on_stack : public callback<R, TArgs...> {
  template <class T>
  concrete_callable_on_stack(T&& fx) noexcept(std::is_nothrow_constructible_v<Fx, T&&>)
      : callback<R, TArgs...>(&executor, &callback_base::stack_deleter),
        _fx(std::forward<T>(fx)) {}

  ~concrete_callable_on_stack() noexcept = default;

 private:
  static R executor(callback_base* base, TArgs... value) {
    return static_cast<concrete_callable_on_stack*>(base)->_fx(std::forward<TArgs>(value)...);
  }

 private:
  Fx _fx;
};

template <typename Fx, typename R, typename... TArgs>
struct callable_impl_noexcept : public callback_noexcept<R, TArgs...> {
  template <class T>
  callable_impl_noexcept(T&& fx) noexcept(std::is_nothrow_constructible_v<Fx, T&&>)
      : callback_noexcept<R, TArgs...>(&executor, get_deleter()),
        _fx(std::forward<T>(fx)) {}

 protected:
  ~callable_impl_noexcept() noexcept = default;

 private:
  static callback_base::deleter_t get_deleter() noexcept {
    if constexpr (std::is_trivially_destructible_v<Fx>) {
      return nullptr;
    } else {
      return +[](callback_base* base) noexcept {
        delete static_cast<callable_impl_noexcept*>(base);
      };
    }
  }

  static R executor(callback_base* base, TArgs... value) noexcept {
    return static_cast<callable_impl_noexcept*>(base)->_fx(std::forward<TArgs>(value)...);
  }

 private:
  Fx _fx;
};

template <typename Fx, typename R, typename... TArgs>
struct callable_on_stack_noexcept : public callback_noexcept<R, TArgs...> {
  template <class T>
  callable_on_stack_noexcept(T&& fx) noexcept(std::is_nothrow_constructible_v<Fx, T&&>)
      : callback_noexcept<R, TArgs...>(&executor, &callback_base::stack_deleter),
        _fx(std::forward<T>(fx)) {}

  ~callable_on_stack_noexcept() noexcept = default;

 private:
  static R executor(callback_base* base, TArgs... value) noexcept {
    return static_cast<callable_on_stack_noexcept*>(base)->_fx(std::forward<TArgs>(value)...);
  }

 private:
  Fx _fx;
};

}  // namespace internal

}  // namespace async_coro
