#pragma once

#include <async_coro/base_handle.h>
#include <async_coro/scheduler.h>

#include <concepts>
#include <coroutine>

namespace async_coro::internal {
struct await_switch_awaitable {
  explicit await_switch_awaitable(execution_thread t) noexcept : thread(t) {}
  await_switch_awaitable(const await_switch_awaitable&) = delete;
  await_switch_awaitable(await_switch_awaitable&&) = delete;

  await_switch_awaitable& operator=(await_switch_awaitable&&) = delete;
  await_switch_awaitable& operator=(const await_switch_awaitable&) = delete;

  bool await_ready() const noexcept { return !need_switch; }

  template <typename U>
	requires(std::derived_from<U, base_handle>)
  void await_suspend(std::coroutine_handle<U> h) {
	if (need_switch) {
	  base_handle& handle = h.promise();
	  handle.get_scheduler().change_thread(handle, thread);
	}
  }

  void await_resume() const noexcept {}

  void embed_task(base_handle& parent) noexcept {
	need_switch = !parent.get_scheduler().is_current_thread_fits(thread);
  }

  execution_thread thread;
  bool need_switch = true;
};
}  // namespace async_coro::internal
