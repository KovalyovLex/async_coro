#pragma once

#include <concepts>

namespace async_coro::internal {

template <typename T>
class passkey {
  friend T;

  passkey() = default;
  explicit passkey(const T* /*this*/) noexcept {}
  ~passkey() noexcept = default;

 public:
  passkey(const passkey&) = delete;
  passkey(passkey&&) = delete;

  passkey& operator=(const passkey&) = delete;
  passkey& operator=(passkey&&) = delete;
};

template <typename T>
passkey(const T*) -> passkey<T>;

template <typename T, typename... Others>
struct passkey_any {
 public:
  template <typename U>
    requires(std::same_as<U, T> || (std::same_as<U, Others> || ...))
  passkey_any(const passkey<U>& /*passkey*/) {}  // NOLINT(*-explicit-*)
};

template <typename T, typename... Others>
struct passkey_successors {
  template <typename U>
    requires(std::derived_from<U, T> || (std::derived_from<U, Others> || ...))
  passkey_successors(const passkey<U>& /*passkey*/) {}  // NOLINT(*-explicit-*)
};

}  // namespace async_coro::internal
