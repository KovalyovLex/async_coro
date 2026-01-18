#include <async_coro/base_handle.h>
#include <async_coro/config.h>
#include <async_coro/internal/coroutine_suspender.h>
#include <async_coro/internal/scheduled_run_data.h>
#include <async_coro/scheduler.h>
#include <async_coro/utils/passkey.h>

#include <atomic>

namespace async_coro::internal {

coroutine_suspender::~coroutine_suspender() noexcept {
  if (_suspend_count.load(std::memory_order::relaxed) != 0) {
    // probably exception was thrown

    // reset our cancel
    _handle->_on_cancel.reset();
  }
}

void coroutine_suspender::try_to_continue_from_any_thread(bool cancel) {
  ASYNC_CORO_ASSERT(_handle);

  if (cancel) {
    _handle->request_cancel();
  }

  dec_num_suspends();
}

void coroutine_suspender::try_to_continue_immediately() {
  ASYNC_CORO_ASSERT(_handle);

  if (!_was_continued_immediately) {
    _was_continued_immediately = true;

    _handle->set_coroutine_state(coroutine_state::suspended);
  }

  dec_num_suspends();
}

void coroutine_suspender::remove_cancel_callback() {
  ASYNC_CORO_ASSERT(_suspend_count.load(std::memory_order::relaxed) > 0);

  // reset our cancel
  _handle->_on_cancel.reset();
}

void coroutine_suspender::dec_num_suspends() {
  const auto prev_count = _suspend_count.fetch_sub(1, std::memory_order::release);
  ASYNC_CORO_ASSERT(prev_count != 0);

  if (prev_count == 1) {
    // sync data with other threads
    (void)_suspend_count.load(std::memory_order::acquire);

    auto handle = std::move(_handle);

    // reset our cancel
    handle->_on_cancel.reset();

    if (handle->is_finished()) [[unlikely]] {
      // some exception happened before callback call
      return;
    }

    handle->get_scheduler().continue_execution(*handle, passkey{this});
  }
}

coroutine_suspender::coroutine_suspender(base_handle& handle, std::uint32_t suspend_count) noexcept
    : _handle(handle.get_owning_ptr()),
      _suspend_count(suspend_count) {
  // try_to_continue_on_same_thread should be called at least once
  ASYNC_CORO_ASSERT(suspend_count > 0);
}

}  // namespace async_coro::internal
