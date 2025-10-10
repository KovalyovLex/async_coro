#pragma once

#include <async_coro/callback.h>
#include <async_coro/config.h>

#include <tuple>

namespace async_coro::internal {

class continue_callback;

using callback_sig = std::tuple<std::unique_ptr<continue_callback, callback_base::deleter>, bool>(bool);

// Callback type to store on stack. For use in continue_after_complete method for advanced awaiters
class continue_callback : public callback<callback_sig> {
  using super = callback<callback_sig>;

 public:
  using ptr = std::unique_ptr<continue_callback, callback_base::deleter>;
  using return_type = std::tuple<ptr, bool>;

  explicit continue_callback(super::executor_t executor, super::deleter_t deleter = nullptr) noexcept
      : super(executor, deleter) {}
};

}  // namespace async_coro::internal
