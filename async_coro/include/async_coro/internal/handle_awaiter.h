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
        _continue_callback(on_continue_callback{*this}) {
  }

  handle_awaiter(const handle_awaiter&) = delete;
  handle_awaiter(handle_awaiter&& other) noexcept
      : _handle(std::move(other._handle)),
        _continue_callback(on_continue_callback{*this}) {
    ASYNC_CORO_ASSERT(other._continue_f == nullptr);
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

  bool await_ready() const noexcept { return _handle.done(); }

  void cancel_await() noexcept {
    _handle.request_cancel();
    _handle.reset_continue();

    if (!_was_continued.exchange(true, std::memory_order::relaxed)) {
      continue_callback::ptr continuation{std::exchange(_continue_f, nullptr)};
      bool cancel = true;

      while (continuation) {
        std::tie(continuation, cancel) = continuation.release()->execute_and_destroy(cancel);
      }
    }
  }

  void continue_after_complete(continue_callback& continue_f) {
    ASYNC_CORO_ASSERT(_continue_f == nullptr);

    _was_continued.store(false, std::memory_order::relaxed);
    _continue_f = &continue_f;

    _handle.continue_with(_continue_callback);
  }

  TRes await_resume() {
    return std::move(_handle).get();
  }

 private:
  struct on_continue_callback {
    void operator()(promise_result<TRes>&, bool canceled) const {
      if (!clb._was_continued.exchange(true, std::memory_order::relaxed)) {
        continue_callback::ptr continuation{std::exchange(clb._continue_f, nullptr)};
        ASYNC_CORO_ASSERT(continuation != nullptr);

        do {
          std::tie(continuation, canceled) = continuation.release()->execute_and_destroy(canceled);
        } while (continuation);
      }
    }

    handle_awaiter& clb;
  };

 private:
  task_handle<TRes> _handle;
  continue_callback* _continue_f = nullptr;
  callback_on_stack<on_continue_callback, void(promise_result<TRes>&, bool)> _continue_callback;
  std::atomic_bool _was_continued{false};
};

}  // namespace async_coro::internal
