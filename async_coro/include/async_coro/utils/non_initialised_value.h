#pragma once

#include <async_coro/config.h>

#include <memory>
#include <type_traits>
#include <utility>

namespace async_coro {

// Template without initialization in constructor.
// Non movable neither copyable.
template <typename T>
union non_initialised_value {
  constexpr non_initialised_value() noexcept {
#if ASYNC_CORO_ASSERT_ENABLED
    _val._initialized = false;
#endif
  }
  non_initialised_value(const non_initialised_value&) = delete;
  non_initialised_value(non_initialised_value&&) = delete;
  non_initialised_value& operator=(const non_initialised_value&) = delete;
  non_initialised_value& operator=(non_initialised_value&&) = delete;
  constexpr ~non_initialised_value() noexcept {
    ASYNC_CORO_ASSERT(!_val._initialized);
  };

  template <class... TArgs>
  constexpr void initialize(TArgs&&... args) noexcept(std::is_nothrow_constructible_v<T, TArgs&&...>) {
    ASYNC_CORO_ASSERT(!_val._initialized);

    auto* address = std::addressof(get_ref());

    new (address) T{std::forward<TArgs>(args)...};

#if ASYNC_CORO_ASSERT_ENABLED
    _val._initialized = true;
#endif
  }

  constexpr void destroy() noexcept(std::is_nothrow_destructible_v<T>) {
    get_value().~T();

#if ASYNC_CORO_ASSERT_ENABLED
    _val._initialized = false;
#endif
  }

  constexpr T& get_value() & noexcept {
    ASYNC_CORO_ASSERT(_val._initialized);

    return get_ref();
  }

  constexpr T&& get_value() && noexcept {
    ASYNC_CORO_ASSERT(_val._initialized);

    return std::move(get_ref());
  }

  constexpr const T& get_value() const& noexcept {
    ASYNC_CORO_ASSERT(_val._initialized);

    return get_ref();
  }

 private:
  constexpr T& get_ref() noexcept {
#if ASYNC_CORO_ASSERT_ENABLED
    return _val._res;
#else
    return _res;
#endif
  }

  constexpr const T& get_ref() const noexcept {
#if ASYNC_CORO_ASSERT_ENABLED
    return _val._res;
#else
    return _res;
#endif
  }

 private:
#if ASYNC_CORO_ASSERT_ENABLED
  struct val {
    T _res;
    bool _initialized;
  };

  val _val;
#else
  T _res;
#endif
};

}  // namespace async_coro
