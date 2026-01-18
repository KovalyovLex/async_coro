#pragma once

#include <async_coro/config.h>
#include <async_coro/utils/always_false.h>
#include <async_coro/utils/non_initialised_value.h>

#include <cstdint>
#include <type_traits>
#include <utility>

namespace async_coro::internal {

template <typename TFunc>
class callback_command_with_args {
  static_assert(always_false<TFunc>::value,
                "callback_command_with_args only accepts function types as template arguments");
};

enum class callback_execute_type : uint8_t {
  execute,
  execute_and_destroy,
  destroy
};

class callback_execute_command {
 protected:
  enum class func_type : uint16_t;

#if ASYNC_CORO_ASSERT_ENABLED
 private:
  static inline uint16_t func_ctr = 1;

  // keeping track of original type and checking conversion
  func_type sig_id{0};

 protected:
#endif

  template <class TFunc>
  static func_type get_type() noexcept {
#if ASYNC_CORO_ASSERT_ENABLED
    static auto type_id = func_ctr++;
    return func_type{type_id};
#else
    return func_type{0};
#endif
  }

  constexpr explicit callback_execute_command(callback_execute_type type, func_type sig_type) noexcept
      :
#if ASYNC_CORO_ASSERT_ENABLED
        sig_id(sig_type),
#endif
        execute(type) {
  }

 public:
  constexpr callback_execute_command() noexcept
      : execute(callback_execute_type::destroy) {}

  callback_execute_command(const callback_execute_command&) = delete;
  callback_execute_command(callback_execute_command&&) = delete;

  callback_execute_command& operator=(const callback_execute_command&) = delete;
  callback_execute_command& operator=(callback_execute_command&&) = delete;

  ~callback_execute_command() noexcept = default;

  template <typename TFunc>
  auto& get_arguments() noexcept;

  const callback_execute_type execute;
};

template <class R, typename... TArgs>
class callback_command_with_args<R(TArgs...)> final : public callback_execute_command {
  using sign_t = R(TArgs...);

 public:
  constexpr explicit callback_command_with_args(callback_execute_type type) noexcept
    requires(sizeof...(TArgs) == 0)
      : callback_execute_command(type, get_type<sign_t>()) {
    args.initialize();
  }

  template <typename... TArgs2>
    requires(sizeof...(TArgs) > 0 && std::is_constructible_v<std::tuple<TArgs...>, TArgs2 && ...>)
  constexpr explicit callback_command_with_args(callback_execute_type type, TArgs2&&... arg) noexcept(std::is_nothrow_constructible_v<std::tuple<TArgs...>, TArgs2&&...>)
      : callback_execute_command(type, get_type<sign_t>()) {
    ASYNC_CORO_ASSERT(type != callback_execute_type::destroy);
    args.initialize(std::forward<TArgs2>(arg)...);
  }

  callback_command_with_args(const callback_command_with_args&) = delete;
  callback_command_with_args(callback_command_with_args&&) = delete;

  callback_command_with_args& operator=(const callback_command_with_args&) = delete;
  callback_command_with_args& operator=(callback_command_with_args&&) = delete;

  ~callback_command_with_args() noexcept(std::is_nothrow_destructible_v<std::tuple<TArgs...>>) {
    if (execute != callback_execute_type::destroy) {
      args.destroy();
      result.destroy();
    }
  }

  constexpr R&& get_result() && noexcept {
    return std::move(result.get_value());
  }

  template <class... TRes>
  constexpr void set_result(TRes&&... args) noexcept(std::is_nothrow_constructible_v<R, TRes&&...>) {
    return result.initialize(std::forward<TRes>(args)...);
  }

  non_initialised_value<R> result;
  non_initialised_value<std::tuple<TArgs...>> args;
};

template <typename... TArgs>
class callback_command_with_args<void(TArgs...)> final : public callback_execute_command {
  using sign_t = void(TArgs...);

 public:
  constexpr explicit callback_command_with_args(callback_execute_type type) noexcept
    requires(sizeof...(TArgs) == 0)
      : callback_execute_command(type, get_type<sign_t>()) {
    args.initialize();
  }

  template <typename... TArgs2>
    requires(sizeof...(TArgs) > 0 && std::is_constructible_v<std::tuple<TArgs...>, TArgs2 && ...>)
  constexpr explicit callback_command_with_args(callback_execute_type type, TArgs2&&... arg) noexcept(std::is_nothrow_constructible_v<std::tuple<TArgs...>, TArgs2&&...>)
      : callback_execute_command(type, get_type<sign_t>()) {
    ASYNC_CORO_ASSERT(type != callback_execute_type::destroy);

    args.initialize(std::forward<TArgs2>(arg)...);
  }

  callback_command_with_args(const callback_command_with_args&) = delete;
  callback_command_with_args(callback_command_with_args&&) = delete;

  callback_command_with_args& operator=(const callback_command_with_args&) = delete;
  callback_command_with_args& operator=(callback_command_with_args&&) = delete;

  ~callback_command_with_args() noexcept(std::is_nothrow_destructible_v<std::tuple<TArgs...>>) {
    if (execute != callback_execute_type::destroy) {
      args.destroy();
    }
  }

  constexpr void get_result() noexcept {}

  non_initialised_value<std::tuple<TArgs...>> args;
};

template <typename TFunc>
inline auto& callback_execute_command::get_arguments() noexcept {
  ASYNC_CORO_ASSERT(execute != callback_execute_type::destroy);
  ASYNC_CORO_ASSERT(sig_id == get_type<TFunc>());

  return *static_cast<callback_command_with_args<TFunc>*>(this);
}

}  // namespace async_coro::internal
