#pragma once

#include <async_coro/base_handle.h>
#include <async_coro/config.h>

#include <coroutine>

namespace async_coro {
class scheduler;
}

namespace async_coro::internal {

struct get_scheduler_awaiter {
  explicit get_scheduler_awaiter() noexcept {}

  get_scheduler_awaiter(const get_scheduler_awaiter&) = delete;
  get_scheduler_awaiter(get_scheduler_awaiter&&) = delete;

  get_scheduler_awaiter& operator=(get_scheduler_awaiter&&) = delete;
  get_scheduler_awaiter& operator=(const get_scheduler_awaiter&) = delete;

  bool await_ready() const noexcept { return true; }

  template <typename U>
  void await_suspend(std::coroutine_handle<U>) noexcept {
    ASYNC_CORO_ASSERT(false);  // we should newer suspend this immediate coroutine
  }

  scheduler& await_resume() noexcept {
    ASYNC_CORO_ASSERT(_ptr);
    return *_ptr;
  }

  get_scheduler_awaiter& coro_await_transform(base_handle& parent) noexcept {
    _ptr = &parent.get_scheduler();
    return *this;
  }

 private:
  scheduler* _ptr = nullptr;
};

}  // namespace async_coro::internal
