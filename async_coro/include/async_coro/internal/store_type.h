#pragma once

#include <async_coro/config.h>

#include <memory>
#include <type_traits>
#if ASYNC_CORO_WITH_EXCEPTIONS
#include <exception>
#endif

namespace async_coro::internal {

template <typename T>
struct result_coro_type {
  union {
    T result;
  };

  result_coro_type() noexcept {}
  result_coro_type(const result_coro_type&) = delete;
  result_coro_type(result_coro_type&&) = delete;
  ~result_coro_type() noexcept {}

  result_coro_type& operator=(const result_coro_type&) = delete;
  result_coro_type& operator=(result_coro_type&&) = delete;

  T& get_ref() noexcept { return result; }
  const T& get_cref() const noexcept { return result; }
  T move() noexcept {
    return std::move(result);
  }

  template <typename... TArgs>
  void inplace_init(TArgs&&... args) noexcept {
    new (&this->result) T(std::forward<TArgs>(args)...);
  }

  void destroy() noexcept(noexcept(std::is_nothrow_destructible_v<T>)) {
    std::destroy_at(&result);
  }
};

template <typename T>
struct result_coro_type<T&> {
  union {
    T* result;
  };

  result_coro_type() noexcept {}
  result_coro_type(const result_coro_type&) = delete;
  result_coro_type(result_coro_type&&) = delete;
  ~result_coro_type() noexcept {}

  result_coro_type& operator=(const result_coro_type&) = delete;
  result_coro_type& operator=(result_coro_type&&) = delete;

  T& get_ref() noexcept { return *result; }
  const T& get_cref() const noexcept { return *result; }
  T& move() noexcept {
    // actually we cant move refs here as the dont hold value
    return *result;
  }

  void inplace_init(T& ref) noexcept {
    this->result = &ref;
  }

  void destroy() noexcept {}
};

#if ASYNC_CORO_WITH_EXCEPTIONS

template <typename T>
struct store_type {
  static inline constexpr bool nothrow_destructible =
      std::is_nothrow_destructible_v<std::exception_ptr> &&
      (std::is_reference_v<T> || std::is_nothrow_destructible_v<T>);

  union {
    std::exception_ptr exception;
    result_coro_type<T> result;
  };

  store_type() noexcept {}
  ~store_type() noexcept {}

  void destroy_exception() noexcept(std::is_nothrow_destructible_v<std::exception_ptr>) {
    std::destroy_at(&exception);
  }

  void destroy_result() noexcept(noexcept(std::is_reference_v<T> || std::is_nothrow_destructible_v<T>)) {
    result.destroy();
  }
};

template <>
struct store_type<void> {
  static inline constexpr bool nothrow_destructible =
      std::is_nothrow_destructible_v<std::exception_ptr>;

  union {
    std::exception_ptr exception;
  };

  store_type() noexcept {}
  ~store_type() noexcept {}

  void destroy_exception() noexcept(std::is_nothrow_destructible_v<std::exception_ptr>) {
    std::destroy_at(&exception);
  }

  void destroy_result() noexcept {}
};

#else

template <typename T>
struct store_type {
  static inline constexpr bool nothrow_destructible =
      std::is_nothrow_destructible_v<T>;

  union {
    result_coro_type<T> result;
  };

  store_type() noexcept {}
  ~store_type() noexcept {}

  void destroy_exception() {}

  void destroy_result() noexcept(noexcept(std::is_reference_v<T> || std::is_nothrow_destructible_v<T>)) {
    result.destroy();
  }
};

template <>
struct store_type<void> {
  static inline constexpr bool nothrow_destructible = true;

  store_type() noexcept {}
  ~store_type() noexcept {}

  void destroy_exception() noexcept {}

  void destroy_result() noexcept {}
};

#endif
}  // namespace async_coro::internal
