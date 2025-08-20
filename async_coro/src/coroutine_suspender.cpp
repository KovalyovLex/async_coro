#include <async_coro/base_handle.h>
#include <async_coro/config.h>
#include <async_coro/coroutine_suspender.h>
#include <async_coro/internal/passkey.h>
#include <async_coro/scheduler.h>

namespace async_coro {

void coroutine_suspender::try_to_continue_on_any_thread() {
  ASYNC_CORO_ASSERT(_handle);

  const auto prev_count = _suspend_count.fetch_sub(1, std::memory_order::relaxed);
  ASYNC_CORO_ASSERT(prev_count != 0);

  if (prev_count == 1) {
    if (_handle->is_finished()) [[unlikely]] {
      // some exception happened before callback call
      return;
    }

    _handle->get_scheduler().continue_execution(*_handle, internal::passkey{this});
  }
}

void coroutine_suspender::try_to_continue_immediately() {
  ASYNC_CORO_ASSERT(_handle);

  bool* was_coro_suspended = nullptr;

  if (!_was_continued_immediately) {
    _was_continued_immediately = true;

    _handle->set_coroutine_state(coroutine_state::suspended);
    was_coro_suspended = std::exchange(_handle->_was_coro_suspended, nullptr);
    if (was_coro_suspended) {
      ASYNC_CORO_ASSERT(*was_coro_suspended == false);

      *was_coro_suspended = true;
    }
  }

  const auto prev_count = _suspend_count.fetch_sub(1, std::memory_order::release);
  ASYNC_CORO_ASSERT(prev_count != 0);

  if (prev_count == 1) {
    if (was_coro_suspended) {
      // return flag as continue execution may throw exception that should be handled in finalizer
      *was_coro_suspended = false;
      _handle->_was_coro_suspended = was_coro_suspended;
    }

    if (_handle->is_finished()) [[unlikely]] {
      // some exception happened before callback call
      return;
    }

    _handle->get_scheduler().continue_execution(*_handle, internal::passkey{this});
  }
}

}  // namespace async_coro
