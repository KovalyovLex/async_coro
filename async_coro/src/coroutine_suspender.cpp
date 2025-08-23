#include <async_coro/base_handle.h>
#include <async_coro/config.h>
#include <async_coro/coroutine_suspender.h>
#include <async_coro/internal/passkey.h>
#include <async_coro/internal/scheduled_run_data.h>
#include <async_coro/scheduler.h>

#include <atomic>

namespace async_coro {

void coroutine_suspender::try_to_continue_from_any_thread() {
  ASYNC_CORO_ASSERT(_handle);

  const auto prev_count = _suspend_count.fetch_sub(1, std::memory_order::release);
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

  internal::scheduled_run_data* run_data = nullptr;

  if (!_was_continued_immediately) {
    _was_continued_immediately = true;

    _handle->set_coroutine_state(coroutine_state::suspended);
    run_data = _handle->_run_data.exchange(nullptr, std::memory_order::relaxed);
    if (run_data) {
      ASYNC_CORO_ASSERT(run_data->external_continuation_request == false);

      run_data->external_continuation_request = true;
    }
  }

  const auto prev_count = _suspend_count.fetch_sub(1, std::memory_order::acq_rel);
  ASYNC_CORO_ASSERT(prev_count != 0);

  if (prev_count == 1) {
    if (run_data) {
      // return flag as continue execution may throw exception that should be handled in finalizer
      run_data->external_continuation_request = false;
      _handle->_run_data.store(run_data, std::memory_order::release);
    }

    if (_handle->is_finished()) [[unlikely]] {
      // some exception happened before callback call
      return;
    }

    _handle->get_scheduler().continue_execution(*_handle, internal::passkey{this});
  }
}

}  // namespace async_coro
