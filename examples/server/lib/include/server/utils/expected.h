#pragma once

#include <async_coro/config.h>

#include <memory>
#include <type_traits>
#include <utility>

namespace server {

struct unexpect_t {
  explicit unexpect_t() = default;
};

inline constexpr unexpect_t unexpect{};

template <class T, class E>
class expected {
  static_assert(!std::is_void_v<E>, "Error can't be void");

  static constexpr auto is_nothrow_destructible = std::is_nothrow_destructible_v<T> && std::is_nothrow_destructible_v<E>;

 public:
  constexpr expected()
      : _has_value(true) {
    std::construct_at(std::addressof(_store.value));
  }

  constexpr expected(const expected& other)
      : _has_value(other._has_value) {
    if (_has_value) {
      std::construct_at(std::addressof(_store.value), other._store.value);
    } else {
      std::construct_at(std::addressof(_store.error), other._store.error);
    }
  }

  constexpr expected(expected&& other) noexcept(std::is_nothrow_move_constructible_v<T> && std::is_nothrow_move_constructible_v<E>)
      : _has_value(other._has_value) {
    if (_has_value) {
      std::construct_at(std::addressof(_store.value), std::move(other._store.value));
    } else {
      std::construct_at(std::addressof(_store.error), std::move(other._store.error));
    }
  }

  template <class U, class G>
    requires(std::is_convertible_v<U, T> && std::is_convertible_v<G, E>)
  constexpr explicit(!std::is_convertible_v<U, T> || !std::is_convertible_v<G, E>) expected(const expected<U, G>& other)
      : _has_value(other.hac_value()) {
    if (_has_value) {
      std::construct_at(std::addressof(_store.value), other.value());
    } else {
      std::construct_at(std::addressof(_store.error), other.error());
    }
  }

  template <class U, class G>
    requires(std::is_convertible_v<U &&, T> && std::is_convertible_v<G &&, E>)
  constexpr explicit(!std::is_convertible_v<U&&, T> || !std::is_convertible_v<G&&, E>) expected(expected<U, G>&& other)
      : _has_value(other.hac_value()) {
    if (_has_value) {
      std::construct_at(std::addressof(_store.value), std::move(other).value());
    } else {
      std::construct_at(std::addressof(_store.error), std::move(other).error());
    }
  }

  template <class U = std::remove_cv_t<T>>
    requires(!std::is_same_v<expected, U>)
  constexpr explicit(!std::is_convertible_v<U, T>) expected(U&& value)
      : _has_value(true) {
    std::construct_at(std::addressof(_store.value), std::forward<U>(value));
  }

  template <class... Args>
  constexpr explicit expected(std::in_place_t /*tag*/, Args&&... args)
      : _has_value(true) {
    std::construct_at(std::addressof(_store.value), std::forward<Args>(args)...);
  }

  template <class... Args>
  constexpr explicit expected(unexpect_t /*tag*/, Args&&... args)
      : _has_value(false) {
    std::construct_at(std::addressof(_store.error), std::forward<Args>(args)...);
  }

  constexpr ~expected() noexcept(is_nothrow_destructible) {
    destroy();
  }

  constexpr expected& operator=(const expected& other) {
    if (this == std::addressof(other)) {
      return *this;
    }

    destroy();
    _has_value = other._has_value;

    if (_has_value) {
      std::construct_at(std::addressof(_store.value), other.value());
    } else {
      std::construct_at(std::addressof(_store.error), other.error());
    }

    return *this;
  }

  constexpr expected& operator=(expected&& other) noexcept(is_nothrow_destructible && std::is_nothrow_move_constructible_v<T> && std::is_nothrow_move_constructible_v<E>) {
    if (this == std::addressof(other)) {
      return *this;
    }

    destroy();
    _has_value = other._has_value;

    if (_has_value) {
      std::construct_at(std::addressof(_store.value), std::move(other).value());
    } else {
      std::construct_at(std::addressof(_store.error), std::move(other).error());
    }

    return *this;
  }

  template <class U = std::remove_cv_t<T>>
  constexpr expected& operator=(U&& value) {
    destroy();
    _has_value = true;

    std::construct_at(std::addressof(_store.value), std::forward<U>(value));

    return *this;
  }

  constexpr explicit operator bool() const noexcept { return _has_value; }

  constexpr const T* operator->() const noexcept {
    ASYNC_CORO_ASSERT(_has_value);
    return std::addressof(_store.value);
  }
  constexpr T* operator->() noexcept {
    ASYNC_CORO_ASSERT(_has_value);
    return std::addressof(_store.value);
  }
  constexpr const T& operator*() const& noexcept {
    ASYNC_CORO_ASSERT(_has_value);
    return _store.value;
  }
  constexpr T& operator*() & noexcept {
    ASYNC_CORO_ASSERT(_has_value);
    return _store.value;
  }
  constexpr const T&& operator*() const&& noexcept {
    ASYNC_CORO_ASSERT(_has_value);
    return std::move(_store.value);
  }
  constexpr T&& operator*() && noexcept {
    ASYNC_CORO_ASSERT(_has_value);
    return std::move(_store.value);
  }

  constexpr T& value() & {
    ASYNC_CORO_ASSERT(_has_value);
    return _store.value;
  }

  constexpr const T& value() const& {
    ASYNC_CORO_ASSERT(_has_value);
    return _store.value;
  }

  constexpr T&& value() && {
    ASYNC_CORO_ASSERT(_has_value);
    return std::move(_store.value);
  }

  constexpr const T&& value() const&& {
    ASYNC_CORO_ASSERT(_has_value);
    return std::move(_store.value);
  }

  template <class U = std::remove_cv_t<T>>
  constexpr T value_or(U&& default_value) const& noexcept(std::is_nothrow_convertible_v<U&&, T>) {
    return _has_value ? _store.value : static_cast<T>(std::forward<U>(default_value));
  }

  template <class U = std::remove_cv_t<T>>
  constexpr T value_or(U&& default_value) && noexcept(std::is_nothrow_convertible_v<U&&, T>) {
    return _has_value ? std::move(_store.value) : static_cast<T>(std::forward<U>(default_value));
  }

  [[nodiscard]] constexpr bool has_value() const noexcept { return _has_value; }

  constexpr const E& error() const& noexcept {
    ASYNC_CORO_ASSERT(!_has_value);
    return _store.error;
  }

  constexpr E& error() & noexcept {
    ASYNC_CORO_ASSERT(!_has_value);
    return _store.error;
  }

  constexpr const E&& error() const&& noexcept {
    ASYNC_CORO_ASSERT(!_has_value);
    return std::move(_store.error);
  }

  constexpr E&& error() && noexcept {
    ASYNC_CORO_ASSERT(!_has_value);
    return std::move(_store.error);
  }

  template <class G = E>
  constexpr E error_or(G&& default_value) const& {
    return _has_value ? std::forward<G>(default_value) : _store.error;
  }

  template <class G = E>
  constexpr E error_or(G&& default_value) && {
    return _has_value ? std::forward<G>(default_value) : std::move(_store.error);
  }

  template <class... Args>
    requires(std::is_nothrow_constructible_v<T, Args...> && is_nothrow_destructible)
  constexpr T& emplace(Args&&... args) noexcept {
    destroy();

    _has_value = true;
    return *std::construct_at(std::addressof(_store.value), std::forward<Args>(args)...);
  }

  constexpr void swap(expected& other) noexcept(std::is_nothrow_move_constructible_v<T> && std::is_nothrow_swappable_v<T> &&
                                                std::is_nothrow_move_constructible_v<E> && std::is_nothrow_swappable_v<E>) {
    if (_has_value == other._has_value) {
      if (_has_value) {
        std::swap(_store.value, other._store.value);
      } else {
        std::swap(_store.error, other._store.error);
      }
      return;
    }

    if (other._has_value && !_has_value) {
      other.swap(*this);
      return;
    }

    // has_value and other !has_value() case

    // Case 1: the move constructions of unexpected values are non-throwing:
    // “other.unex” will be restored if the construction of “other.val” fails
    if constexpr (std::is_nothrow_move_constructible_v<E>) {
      E temp(std::move(other._store.error));
      std::destroy_at(std::addressof(other._store.error));
      try {
        std::construct_at(std::addressof(other._store.value), std::move(_store.value));  // may throw
        std::destroy_at(std::addressof(_store.value));
        std::construct_at(std::addressof(_store.error), std::move(temp));
      } catch (...) {
        std::construct_at(std::addressof(other._store.error), std::move(temp));
        throw;
      }
    }
    // Case 2: the move constructions of expected values are non-throwing:
    // “this->val” will be restored if the construction of “this->unex” fails
    else {
      T temp(std::move(_store.value));
      std::destroy_at(std::addressof(_store.value));
      try {
        std::construct_at(std::addressof(_store.error), std::move(other._store.error));  // may throw
        std::destroy_at(std::addressof(other._store.error));
        std::construct_at(std::addressof(other._store.value), std::move(temp));
      } catch (...) {
        std::construct_at(std::addressof(_store.value), std::move(temp));
        throw;
      }
    }
    _has_value = false;
    other._has_value = true;
  }

 private:
  constexpr void destroy() noexcept(is_nothrow_destructible) {
    if (_has_value) {
      std::destroy_at(std::addressof(_store.value));
    } else {
      std::destroy_at(std::addressof(_store.error));
    }
  }

 private:
  union storage {          // NOLINT(*-member-functions)
    storage() noexcept {}  // NOLINT(*-member-init)
    ~storage() noexcept {}

    T value;
    E error;
  };

  storage _store;
  bool _has_value;
};

template <class T, class E>
  requires std::is_void_v<T>
class expected<T, E> {
  static_assert(!std::is_void_v<E>, "Error can't be void");

  static constexpr auto is_nothrow_destructible = std::is_nothrow_destructible_v<E>;

 public:
  constexpr expected()
      : _has_value(true) {
  }

  constexpr expected(const expected& other)
      : _has_value(other._has_value) {
    if (!_has_value) {
      std::construct_at(std::addressof(_store.error), other._store.error);
    }
  }

  constexpr expected(expected&& other) noexcept(std::is_nothrow_move_constructible_v<T> && std::is_nothrow_move_constructible_v<E>)
      : _has_value(other._has_value) {
    if (!_has_value) {
      std::construct_at(std::addressof(_store.error), std::move(other._store.error));
    }
  }

  template <class G>
    requires(std::is_convertible_v<G, E>)
  constexpr explicit(!std::is_convertible_v<G, E>) expected(const expected<void, G>& other)
      : _has_value(other.hac_value()) {
    if (!_has_value) {
      std::construct_at(std::addressof(_store.error), other.error());
    }
  }

  template <class G>
    requires(std::is_convertible_v<G &&, E>)
  constexpr explicit(!std::is_convertible_v<G&&, E>) expected(expected<void, G>&& other)
      : _has_value(other.hac_value()) {
    if (!_has_value) {
      std::construct_at(std::addressof(_store.error), std::move(other).error());
    }
  }

  template <class... Args>
  constexpr explicit expected(unexpect_t /*tag*/, Args&&... args)
      : _has_value(false) {
    std::construct_at(std::addressof(_store.error), std::forward<Args>(args)...);
  }

  constexpr ~expected() noexcept(is_nothrow_destructible) {
    destroy();
  }

  constexpr expected& operator=(const expected& other) {
    if (this == std::addressof(other)) {
      return *this;
    }

    destroy();
    _has_value = other._has_value;

    if (_has_value) {
      if constexpr (!std::is_void_v<T>) {
        std::construct_at(std::addressof(_store.value), other.value());
      }
    } else {
      std::construct_at(std::addressof(_store.error), other.error());
    }

    return *this;
  }

  constexpr expected& operator=(expected&& other) noexcept(is_nothrow_destructible && std::is_nothrow_move_constructible_v<T> && std::is_nothrow_move_constructible_v<E>) {
    if (this == std::addressof(other)) {
      return *this;
    }

    destroy();
    _has_value = other._has_value;

    if (_has_value) {
      if constexpr (!std::is_void_v<T>) {
        std::construct_at(std::addressof(_store.value), std::move(other).value());
      }
    } else {
      std::construct_at(std::addressof(_store.error), std::move(other).error());
    }

    return *this;
  }

  template <class U = std::remove_cv_t<T>>
  constexpr expected& operator=(U&& value) {
    destroy();
    _has_value = true;

    std::construct_at(std::addressof(_store.value), std::forward<U>(value));

    return *this;
  }

  constexpr explicit operator bool() const noexcept { return _has_value; }

  constexpr void operator*() const noexcept {
    ASYNC_CORO_ASSERT(_has_value);
  }

  constexpr void value() const& {
    ASYNC_CORO_ASSERT(_has_value);
    return _store.value;
  }

  constexpr void value() && {
    ASYNC_CORO_ASSERT(_has_value);
  }

  constexpr const E& error() const& noexcept {
    ASYNC_CORO_ASSERT(!_has_value);
    return _store.error;
  }

  constexpr E& error() & noexcept {
    ASYNC_CORO_ASSERT(!_has_value);
    return _store.error;
  }

  constexpr const E&& error() const&& noexcept {
    ASYNC_CORO_ASSERT(!_has_value);
    return std::move(_store.error);
  }

  constexpr E&& error() && noexcept {
    ASYNC_CORO_ASSERT(!_has_value);
    return std::move(_store.error);
  }

  template <class G = E>
  constexpr E error_or(G&& default_value) const& {
    return _has_value ? std::forward<G>(default_value) : _store.error;
  }

  template <class G = E>
  constexpr E error_or(G&& default_value) && {
    return _has_value ? std::forward<G>(default_value) : std::move(_store.error);
  }

  constexpr void emplace() noexcept(is_nothrow_destructible) {
    _has_value = true;
    destroy();
  }

  constexpr void swap(expected& other) noexcept(std::is_nothrow_move_constructible_v<T> && std::is_nothrow_swappable_v<T> &&
                                                std::is_nothrow_move_constructible_v<E> && std::is_nothrow_swappable_v<E>) {
    if (_has_value == other._has_value) {
      if (!_has_value) {
        std::swap(_store.error, other._store.error);
      }
      return;
    }

    if (other._has_value && !_has_value) {
      other.swap(*this);
      return;
    }

    // has_value and other !has_value() case

    std::construct_at(std::addressof(_store.error), std::move(other._store.error));
    std::destroy_at(std::addressof(other._store.error));
    _has_value = false;
    other._has_value = true;
  }

 private:
  constexpr void destroy() noexcept(is_nothrow_destructible) {
    if (!_has_value) {
      std::destroy_at(std::addressof(_store.error));
    }
  }

 private:
  union storage {  // NOLINT(*-member-functions)
    storage() noexcept {}
    ~storage() noexcept {}

    E error;
  };

  storage _store;
  bool _has_value;
};

}  // namespace server
