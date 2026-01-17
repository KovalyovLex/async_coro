#pragma once

#include <async_coro/callback.h>
#include <async_coro/config.h>
#include <async_coro/internal/advanced_awaiter.h>
#include <async_coro/internal/continue_callback.h>
#include <async_coro/internal/type_traits.h>

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
      : _handle(std::move(handle)),
        _continue_callback(on_continue_callback{this}) {
  }

  handle_awaiter(const handle_awaiter&) = delete;
  handle_awaiter(handle_awaiter&& other) noexcept
      : _handle(std::move(other._handle)),
        _continue_callback(on_continue_callback{this}) {
    ASYNC_CORO_ASSERT(other._continue_f == nullptr);
  }

  ~handle_awaiter() noexcept = default;

  handle_awaiter& operator=(const handle_awaiter&) = delete;
  handle_awaiter& operator=(handle_awaiter&&) = delete;

  [[nodiscard]] bool adv_await_ready() const noexcept { return _handle.done(); }

  void cancel_adv_await() {
    // remove our continuation (but it can be continued after reset, thats why we clear _self_handle only in callback)
    _handle.reset_continue();

    // cancel execution of task
    _handle.request_cancel();
  }

  void adv_await_suspend(continue_callback& continue_f) {
    ASYNC_CORO_ASSERT(_continue_f.load(std::memory_order::relaxed) == nullptr);

    // barrier to prevent reordering
    _continue_f.store(&continue_f, std::memory_order::release);

    _handle.continue_with(_continue_callback);
  }

  TRes adv_await_resume() {
    return std::move(_handle).get();
  }

 private:
  class on_continue_callback : public callback<void(promise_result<TRes>&, bool)> {
    using super = callback<void(promise_result<TRes>&, bool)>;

   public:
    explicit on_continue_callback(handle_awaiter* awaiter) noexcept
        : super(&on_execute, &on_destroy),
          _awaiter(awaiter) {}

   private:
    static void on_execute(callback_base* base, ASYNC_CORO_ASSERT_VARIABLE bool with_destroy, promise_result<TRes>& /*result*/, bool cancelled) {
      ASYNC_CORO_ASSERT(with_destroy);

      auto* clb = static_cast<on_continue_callback*>(base)->_awaiter;

      continue_callback::ptr continuation{clb->_continue_f.exchange(nullptr, std::memory_order::relaxed)};
      ASYNC_CORO_ASSERT(continuation != nullptr);

      while (continuation) {
        std::tie(continuation, cancelled) = continuation.release()->execute_and_destroy(cancelled);
      }
    }

    static void on_destroy(callback_base* base) noexcept {
      auto* clb = static_cast<on_continue_callback*>(base)->_awaiter;

      // destroy continuation
      continue_callback::ptr continuation{clb->_continue_f.exchange(nullptr, std::memory_order::relaxed)};
    }

   private:
    handle_awaiter* _awaiter;
  };

 private:
  task_handle<TRes> _handle;
  std::atomic<continue_callback*> _continue_f = nullptr;
  on_continue_callback _continue_callback;
};

}  // namespace async_coro::internal
