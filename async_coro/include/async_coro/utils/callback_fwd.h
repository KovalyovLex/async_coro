#pragma once

#include <async_coro/internal/callback_execute_command.h>
#include <async_coro/utils/always_false.h>

#include <type_traits>

namespace async_coro {

template <typename TFunc>
class callback {
  static_assert(always_false<TFunc>::value,
                "callback only accepts function types as template arguments, "
                "with possibly noexcept qualifiers.");
};

// Base class to store type erased callback and call destroy
template <bool Noexcept>
class callback_base {
 public:
  static constexpr bool is_noexcept = Noexcept;

  using executor_t = std::conditional_t<is_noexcept, void (*)(internal::callback_execute_command&, callback_base&) noexcept, void (*)(internal::callback_execute_command&, callback_base&)>;

 protected:
  constexpr explicit callback_base(executor_t exec) noexcept
      : _executor(exec) {}

 public:
  void destroy() noexcept(is_noexcept) {
    internal::callback_execute_command arg;
    _executor(arg, *this);
  }

 protected:
  executor_t _executor;
};

/**
 * @brief A type-erased callback that takes arguments TArgs... and returns R.
 * Not supposed to be used as concrete type. For callbacks use callback_on_stack or callback_on_heap
 * @tparam R The return type of the callback.
 * @tparam TArgs The argument types of the callback.
 */
template <typename R, typename... TArgs>
class callback<R(TArgs...)> : public callback_base<false> {
  using arg_t = internal::callback_command_with_args<R(TArgs...)>;

 public:
  using return_type = R;
  using execute_signature = R(TArgs...);
  using callback_signature = R(TArgs...);

  /**
   * @brief Constructs a new callback object.
   * @param executor The function that will be executed when the callback is
   * invoked.
   * @param deleter A custom deleter function.
   */
  constexpr explicit callback(executor_t executor) noexcept
      : callback_base(executor) {
  }

  R execute(TArgs... value) {
    arg_t arg{internal::callback_execute_type::execute, std::forward<TArgs>(value)...};
    _executor(arg, *this);
    return std::move(arg).get_result();
  }

  R execute_and_destroy(TArgs... value) {
    arg_t arg{internal::callback_execute_type::execute_and_destroy, std::forward<TArgs>(value)...};
    _executor(arg, *this);
    return std::move(arg).get_result();
  }
};

// Noexcept overload
template <typename R, typename... TArgs>
class callback<R(TArgs...) noexcept> : public callback_base<true> {
  using arg_t = internal::callback_command_with_args<R(TArgs...)>;

 public:
  using return_type = R;
  using execute_signature = R(TArgs...);
  using callback_signature = R(TArgs...) noexcept;

  constexpr explicit callback(executor_t executor) noexcept
      : callback_base(executor) {
  }

  R execute(TArgs... value) noexcept {
    arg_t arg{internal::callback_execute_type::execute, std::forward<TArgs>(value)...};
    _executor(arg, *this);
    return std::move(arg).get_result();
  }

  R execute_and_destroy(TArgs... value) noexcept {
    arg_t arg{internal::callback_execute_type::execute_and_destroy, std::forward<TArgs>(value)...};
    _executor(arg, *this);
    return std::move(arg).get_result();
  }
};

}  // namespace async_coro
