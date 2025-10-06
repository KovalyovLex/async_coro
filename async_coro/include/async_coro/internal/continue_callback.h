#pragma once

#include <async_coro/callback.h>
#include <async_coro/config.h>

#include <tuple>

namespace async_coro::internal {

template <class... TArgs>
class recurrent_callback : public callback<std::tuple<std::unique_ptr<recurrent_callback<TArgs...>, callback_base::deleter>, TArgs...>, TArgs...> {
  using super = callback<std::tuple<std::unique_ptr<recurrent_callback<TArgs...>, callback_base::deleter>, TArgs...>, TArgs...>;

 public:
  using ptr = std::unique_ptr<recurrent_callback<TArgs...>, callback_base::deleter>;
  using return_type = std::tuple<ptr, TArgs...>;

  recurrent_callback(super::executor_t executor, super::deleter_t deleter = nullptr) noexcept
      : super(executor, deleter) {}
};

template <typename Fx, typename... TArgs>
class recurrent_callback_on_stack : public recurrent_callback<TArgs...> {
  using super = recurrent_callback<TArgs...>;

 public:
  template <class... TArgs2>
  recurrent_callback_on_stack(TArgs2&&... args) noexcept(std::is_nothrow_constructible_v<Fx, TArgs2&&...>)
      : super(&executor, nullptr),
        _fx(std::forward<TArgs2>(args)...) {}

  recurrent_callback_on_stack(const recurrent_callback_on_stack&) = delete;
  recurrent_callback_on_stack(recurrent_callback_on_stack&&) = delete;

  recurrent_callback_on_stack& operator=(const recurrent_callback_on_stack&) = delete;
  recurrent_callback_on_stack& operator=(recurrent_callback_on_stack&&) = delete;

  ~recurrent_callback_on_stack() noexcept = default;

 private:
  static super::return_type executor(callback_base* base, TArgs... value) {
    return static_cast<recurrent_callback_on_stack*>(base)->_fx(std::forward<TArgs>(value)...);
  }

 private:
  Fx _fx;
};

using continue_callback = recurrent_callback<bool>;

template <typename Fx>
using continue_callback_on_stack = recurrent_callback_on_stack<Fx, bool>;

}  // namespace async_coro::internal
