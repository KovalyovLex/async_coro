#pragma once

#include <async_coro/config.h>
#include <async_coro/internal/advanced_awaiter.h>
#include <async_coro/internal/continue_callback.h>
#include <async_coro/internal/type_traits.h>
#include <async_coro/utils/callback_on_stack.h>

#include <atomic>
#include <tuple>
#include <utility>

namespace async_coro {
template <class R>
class task_handle;

template <class T>
class promise_result;
}  // namespace async_coro

namespace async_coro::internal {

// wrapper for single task to await with operators || and &&
template <class TRes>
class handle_awaiter : public advanced_awaiter<handle_awaiter<TRes>> {
 public:
  using result_type = TRes;

  explicit handle_awaiter(task_handle<TRes> handle) noexcept
      : _handle(std::move(handle)) {
  }

  handle_awaiter(const handle_awaiter&) = delete;
  handle_awaiter(handle_awaiter&& other) noexcept
      : _handle(std::move(other._handle)) {
    ASYNC_CORO_ASSERT(other._continue_f == nullptr);
  }

  ~handle_awaiter() noexcept = default;

  handle_awaiter& operator=(const handle_awaiter&) = delete;
  handle_awaiter& operator=(handle_awaiter&&) = delete;

  [[nodiscard]] bool adv_await_ready() const noexcept { return _handle.done(); }

  void cancel_adv_await() {
    // remove our continuation (but it can be continued after reset, thats why we clear _continue_f only in callback)
    _handle.reset_continue();

    // cancel execution of task
    _handle.request_cancel();
  }

  void adv_await_suspend(continue_callback_ptr continue_f, async_coro::base_handle& /*handle*/) {
    ASYNC_CORO_ASSERT(_continue_f == nullptr);

    // barrier to prevent reordering
    _continue_f.reset(continue_f.release(), std::memory_order::release);

    _handle.continue_with(_continue_callback.get_ptr());
  }

  TRes adv_await_resume() {
    return std::move(_handle).get();
  }

 private:
  using callback_t = callback<void(promise_result<TRes>&, bool)>;

  class external_continue_callback : public callback_on_stack<external_continue_callback, callback_t> {
   public:
    void on_destroy() {
      auto& awaiter = this->get_owner(&handle_awaiter::_continue_callback);

      // destroy continuation
      continue_callback_holder continuation{std::move(awaiter._continue_f)};
    }

    void on_execute_and_destroy(promise_result<TRes>& /*result*/, bool cancelled) {
      auto& awaiter = this->get_owner(&handle_awaiter::_continue_callback);

      continue_callback_holder continuation{std::move(awaiter._continue_f)};
      ASYNC_CORO_ASSERT(continuation);

      while (continuation) {
        std::tie(continuation, cancelled) = continuation.execute_and_destroy(cancelled);
      }
    }
  };

 private:
  task_handle<TRes> _handle;
  continue_callback_atomic_ptr _continue_f = nullptr;
  external_continue_callback _continue_callback;
};

}  // namespace async_coro::internal
