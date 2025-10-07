#pragma once

#include <async_coro/config.h>
#include <async_coro/internal/deduce_function_signature.h>
#include <async_coro/internal/type_traits.h>

#include <memory>
#include <type_traits>
#include <utility>

namespace async_coro {

namespace internal {
template <typename Fx, bool Noexcept, typename TFunc>
class callback_impl;

template <bool Noexcept, typename TFunc>
class callback_def;

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
  class deleter {
   public:
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
  callback_base(deleter_t deleter = &default_deleter) noexcept : _deleter(deleter) {}
  callback_base(const callback_base&) noexcept = default;
  callback_base(callback_base&&) noexcept = default;

  /**
   * @brief Destroys the callback object using either the custom deleter or the
   * default delete operator.
   */
  void destroy() noexcept;

 protected:
  ~callback_base() noexcept = default;

  deleter_t get_deleter() const noexcept { return _deleter; }

  static void default_deleter(callback_base*) noexcept;

 private:
  deleter_t _deleter;
};

template <typename TFunc>
class callback {
  static_assert(internal::always_false<TFunc>::value,
                "callback only accepts function types as template arguments, "
                "with possibly noexcept qualifiers.");
};

template <bool Noexcept, typename R, typename... TArgs>
class internal::callback_def<Noexcept, R(TArgs...)> : public callback_base {
  using callback_t = std::conditional_t<Noexcept, callback<R(TArgs...) noexcept>, callback<R(TArgs...)>>;

 public:
  /**
   * @brief A unique pointer to a callback that uses the custom deleter.
   */
  using ptr = std::unique_ptr<callback_t, callback_base::deleter>;

  /**
   * @brief A function pointer type for the callback's executor function.
   */
  using executor_t = R (*)(callback_base*, bool /*with_destroy*/, TArgs...) noexcept(Noexcept);

 protected:
  callback_def(executor_t executor, deleter_t deleter = &callback_base::default_deleter) noexcept
      : callback_base(deleter),
        _executor(executor) {
    ASYNC_CORO_ASSERT(executor);
  }

 public:
  /**
   * @brief Executes the callback with the given arguments.
   * @param value The arguments to pass to the callback.
   */
  R execute(TArgs... value) noexcept(Noexcept) {
    return _executor(this, false, std::forward<TArgs>(value)...);
  }

  /**
   * @brief Executes the callback with the given arguments and calls destroy in safe way.
   * @param value The arguments to pass to the callback.
   */
  R execute_and_destroy(TArgs... value) noexcept(Noexcept) {
    return _executor(this, true, std::forward<TArgs>(value)...);
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
  static callback_t* allocate(Fx&& fx) {
    return new internal::callback_impl<std::remove_cvref_t<Fx>, Noexcept, R(TArgs...)>(std::forward<Fx>(fx));
  }

 protected:
  ~callback_def() noexcept = default;

 private:
  executor_t _executor;
};

/**
 * @brief A type-erased callback that takes arguments TArgs... and returns R.
 * @tparam R The return type of the callback.
 * @tparam TArgs The argument types of the callback.
 */
template <typename R, typename... TArgs>
class callback<R(TArgs...)> : public internal::callback_def<false, R(TArgs...)> {
  using super = internal::callback_def<false, R(TArgs...)>;

 public:
  /**
   * @brief Constructs a new callback object.
   * @param executor The function that will be executed when the callback is
   * invoked.
   * @param deleter A custom deleter function.
   */
  callback(super::executor_t executor, super::deleter_t deleter = &callback_base::default_deleter) noexcept
      : super(executor, deleter) {
  }
};

template <typename R, typename... TArgs>
class callback<R(TArgs...) noexcept> : public internal::callback_def<true, R(TArgs...)> {
  using super = internal::callback_def<true, R(TArgs...)>;

 public:
  /**
   * @brief Constructs a new callback object.
   * @param executor The function that will be executed when the callback is
   * invoked.
   * @param deleter A custom deleter function.
   */
  callback(super::executor_t executor, super::deleter_t deleter = &callback_base::default_deleter) noexcept
      : super(executor, deleter) {
  }
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

struct callback_raii_deleter {
  ~callback_raii_deleter() noexcept {
    callback.destroy();
  }
  callback_base& callback;
};

template <typename Fx, bool Noexcept, typename R, typename... TArgs>
class callback_impl<Fx, Noexcept, R(TArgs...)> final : public std::conditional_t<Noexcept, callback<R(TArgs...) noexcept>, callback<R(TArgs...)>> {
  using super = std::conditional_t<Noexcept, callback<R(TArgs...) noexcept>, callback<R(TArgs...)>>;

 public:
  template <class T>
  callback_impl(T&& fx) noexcept(std::is_nothrow_constructible_v<Fx, T&&>)
      : super(&executor, get_deleter()),
        _fx(std::forward<T>(fx)) {}

 protected:
  ~callback_impl() noexcept = default;

 private:
  static callback_base::deleter_t get_deleter() noexcept {
    if constexpr (std::is_trivially_destructible_v<Fx>) {
      return &callback_base::default_deleter;
    } else {
      return +[](callback_base* base) noexcept {
        delete static_cast<callback_impl*>(base);
      };
    }
  }

  static R executor(callback_base* base, bool with_destroy, TArgs... value) noexcept(Noexcept) {
    callback_impl& clb = *static_cast<callback_impl*>(base);

    if (with_destroy) {
      callback_raii_deleter t{clb};
      return clb._fx(std::forward<TArgs>(value)...);
    } else {
      return clb._fx(std::forward<TArgs>(value)...);
    }
  }

 private:
  Fx _fx;
};

template <typename Fx, typename TFunc>
class callback_on_stack {
  static_assert(internal::always_false<TFunc>::value,
                "callback_on_stack only accepts function types as template arguments, "
                "with possibly noexcept qualifiers.");
};

template <typename Fx, typename R, typename... TArgs>
class callback_on_stack<Fx, R(TArgs...)> : public callback<R(TArgs...)> {
 public:
  template <class... TArgs2>
  callback_on_stack(TArgs2&&... args) noexcept(std::is_nothrow_constructible_v<Fx, TArgs2&&...>)
      : callback<R(TArgs...)>(&executor, nullptr),
        _fx(std::forward<TArgs2>(args)...) {}

  callback_on_stack(const callback_on_stack&) = delete;
  callback_on_stack(callback_on_stack&&) = delete;

  callback_on_stack& operator=(const callback_on_stack&) = delete;
  callback_on_stack& operator=(callback_on_stack&&) = delete;

  ~callback_on_stack() noexcept = default;

 private:
  static R executor(callback_base* base, bool, TArgs... value) {
    // no destroy need
    return static_cast<callback_on_stack*>(base)->_fx(std::forward<TArgs>(value)...);
  }

 private:
  Fx _fx;
};

template <typename Fx, typename R, typename... TArgs>
class callback_on_stack<Fx, R(TArgs...) noexcept> : public callback<R(TArgs...) noexcept> {
 public:
  template <class... TArgs2>
  callback_on_stack(TArgs2&&... args) noexcept(std::is_nothrow_constructible_v<Fx, TArgs2&&...>)
      : callback<R(TArgs...)>(&executor, nullptr),
        _fx(std::forward<TArgs2>(args)...) {}

  callback_on_stack(const callback_on_stack&) = delete;
  callback_on_stack(callback_on_stack&&) = delete;

  callback_on_stack& operator=(const callback_on_stack&) = delete;
  callback_on_stack& operator=(callback_on_stack&&) = delete;

  ~callback_on_stack() noexcept = default;

 private:
  static R executor(callback_base* base, bool, TArgs... value) noexcept {
    // no destroy need
    return static_cast<callback_on_stack*>(base)->_fx(std::forward<TArgs>(value)...);
  }

 private:
  Fx _fx;
};

}  // namespace internal

}  // namespace async_coro
