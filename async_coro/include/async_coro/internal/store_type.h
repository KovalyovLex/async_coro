#pragma once

#include <async_coro/config.h>

#include <memory>
#include <type_traits>
#if !ASYNC_CORO_NO_EXCEPTIONS
#include <exception>
#endif

namespace async_coro::internal {

template <typename T>
struct result_coro_type {
  T result;

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
  T* result;

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

#if ASYNC_CORO_NO_EXCEPTIONS
template <typename T>
union store_type {
  static inline constexpr bool nothrow_destructible =
      std::is_nothrow_destructible_v<T>;

  T result;

  store_type() noexcept {}
  ~store_type() noexcept {}
  void destroy_exception() {}
  void destroy_result() noexcept(noexcept(std::is_nothrow_destructible_v<T>)) {
    if constexpr (!std::is_reference_v<T>) {
      std::destroy_at(&result);
    }
  }
};

template <>
union store_type<void> {
  static inline constexpr bool nothrow_destructible = true;

  store_type() noexcept {}
  ~store_type() noexcept {}
  void destroy_exception() noexcept {}
  void destroy_result() noexcept {}
};
#else
template <typename T>
union store_type {
  static inline constexpr bool nothrow_destructible =
      std::is_nothrow_destructible_v<std::exception_ptr> &&
      std::is_nothrow_destructible_v<T>;

  std::exception_ptr exception;
  result_coro_type<T> result;

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
union store_type<void> {
  static inline constexpr bool nothrow_destructible =
      std::is_nothrow_destructible_v<std::exception_ptr>;

  std::exception_ptr exception;

  store_type() noexcept {}
  ~store_type() noexcept {}
  void destroy_exception() noexcept(
      std::is_nothrow_destructible_v<std::exception_ptr>) {
    std::destroy_at(&exception);
  }
  void destroy_result() noexcept {}
};
#endif
}  // namespace async_coro::internal
