#pragma once

#include <async_coro/callback.h>
#include <async_coro/config.h>
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

template <class... TAwaiters>
class all_awaiter;

template <class... TAwaiters>
class any_awaiter;

// wrapper for single task to await with operators || and &&
template <class TRes>
class handle_awaiter {
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

  ~handle_awaiter() noexcept {
    _can_be_freed.wait(false, std::memory_order::acquire);
  }

  handle_awaiter& operator=(const handle_awaiter&) = delete;
  handle_awaiter& operator=(handle_awaiter&&) = delete;

  template <class TRes2>
  auto operator&&(task_handle<TRes2>&& other) && noexcept {
    return all_awaiter{std::tuple<handle_awaiter<TRes>, handle_awaiter<TRes2>>{std::move(*this), handle_awaiter<TRes2>{std::move(other)}}};
  }

  template <class TRes2>
  auto operator||(task_handle<TRes2>&& other) && noexcept {
    return any_awaiter{std::tuple<handle_awaiter<TRes>, handle_awaiter<TRes2>>{std::move(*this), handle_awaiter<TRes2>{std::move(other)}}};
  }

  [[nodiscard]] bool await_ready() const noexcept { return _handle.done(); }

  void cancel_await() {
    _handle.request_cancel();
    _handle.reset_continue();

    if (!_was_continued.exchange(true, std::memory_order::acquire)) {
      continue_callback::ptr continuation{std::exchange(_continue_f, nullptr)};
      bool cancel = true;

      while (continuation) {
        std::tie(continuation, cancel) = continuation.release()->execute_and_destroy(cancel);
      }
    }
  }

  void continue_after_complete(continue_callback& continue_f) {
    ASYNC_CORO_ASSERT(_continue_f == nullptr);

    _continue_f = &continue_f;
    _can_be_freed.store(false, std::memory_order::relaxed);
    _was_continued.store(false, std::memory_order::release);

    _handle.continue_with(_continue_callback);
  }

  TRes await_resume() {
    return std::move(_handle).get();
  }

 private:
  void set_can_be_freed() noexcept {
    if (!_can_be_freed.exchange(true, std::memory_order::release)) {
      _can_be_freed.notify_one();
    }
  }

 private:
  class on_continue_callback : public callback<void(promise_result<TRes>&, bool)> {
    using super = callback<void(promise_result<TRes>&, bool)>;

   public:
    explicit on_continue_callback(handle_awaiter* awaiter) noexcept
        : super(&executor, &deleter),
          _awaiter(awaiter) {}

   private:
    static void executor(callback_base* base, ASYNC_CORO_ASSERT_VARIABLE bool with_destroy, promise_result<TRes>& /*result*/, bool cancelled) {
      ASYNC_CORO_ASSERT(with_destroy);

      auto* clb = static_cast<on_continue_callback*>(base)->_awaiter;

      if (!clb->_was_continued.exchange(true, std::memory_order::relaxed)) {
        continue_callback::ptr continuation{std::exchange(clb->_continue_f, nullptr)};
        ASYNC_CORO_ASSERT(continuation != nullptr);

        clb->set_can_be_freed();

        while (continuation) {
          std::tie(continuation, cancelled) = continuation.release()->execute_and_destroy(cancelled);
        }
      } else {
        clb->set_can_be_freed();
      }
    }

    static void deleter(callback_base* base) noexcept {
      static_cast<on_continue_callback*>(base)->_awaiter->set_can_be_freed();
    }

   private:
    handle_awaiter* _awaiter;
  };

 private:
  std::atomic_bool _can_be_freed{true};
  task_handle<TRes> _handle;
  continue_callback* _continue_f = nullptr;
  on_continue_callback _continue_callback;
  std::atomic_bool _was_continued{false};
};

}  // namespace async_coro::internal
