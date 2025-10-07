#pragma once

#include <async_coro/callback.h>
#include <async_coro/config.h>

#include <tuple>

namespace async_coro::internal {

class continue_callback;

using callback_sig = std::tuple<std::unique_ptr<continue_callback, callback_base::deleter>, bool>(bool);

class continue_callback : public callback<callback_sig> {
  using super = callback<callback_sig>;

 public:
  using ptr = std::unique_ptr<continue_callback, callback_base::deleter>;
  using return_type = std::tuple<ptr, bool>;

  continue_callback(super::executor_t executor, super::deleter_t deleter = nullptr) noexcept
      : super(executor, deleter) {}
};

template <typename Fx>
class continue_callback_on_stack : public continue_callback {
 public:
  template <class... TArgs2>
  continue_callback_on_stack(TArgs2&&... args) noexcept(std::is_nothrow_constructible_v<Fx, TArgs2&&...>)
      : continue_callback(&executor, nullptr),
        _fx(std::forward<TArgs2>(args)...) {}

  continue_callback_on_stack(const continue_callback_on_stack&) = delete;
  continue_callback_on_stack(continue_callback_on_stack&&) = delete;

  continue_callback_on_stack& operator=(const continue_callback_on_stack&) = delete;
  continue_callback_on_stack& operator=(continue_callback_on_stack&&) = delete;

  ~continue_callback_on_stack() noexcept = default;

 private:
  static continue_callback::return_type executor(callback_base* base, bool /*with_destroy*/, bool cancel) {
    return static_cast<continue_callback_on_stack*>(base)->_fx(cancel);
  }

 private:
  Fx _fx;
};

}  // namespace async_coro::internal
