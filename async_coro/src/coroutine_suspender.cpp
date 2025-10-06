#include <async_coro/base_handle.h>
#include <async_coro/config.h>
#include <async_coro/coroutine_suspender.h>
#include <async_coro/internal/passkey.h>
#include <async_coro/internal/scheduled_run_data.h>
#include <async_coro/scheduler.h>

#include <atomic>

namespace async_coro {

coroutine_suspender::~coroutine_suspender() noexcept {
  if (_suspend_count.load(std::memory_order::relaxed) != 0) {
    // probably exception was thrown

    // reset our cancel
    if (auto cancel = _handle->_on_cancel.exchange(nullptr, std::memory_order::relaxed)) {
      cancel->destroy();
    }
  }
}

void coroutine_suspender::try_to_continue_from_any_thread(bool cancel) {
  ASYNC_CORO_ASSERT(_handle);

  const auto prev_count = _suspend_count.fetch_sub(1, std::memory_order::release);
  ASYNC_CORO_ASSERT(prev_count != 0);

  if (cancel) {
    _handle->request_cancel();
  }

  if (prev_count == 1) {
    (void)_suspend_count.load(std::memory_order::acquire);

    // reset our cancel
    if (auto on_cancel = _handle->_on_cancel.exchange(nullptr, std::memory_order::relaxed)) {
      on_cancel->destroy();
    }

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

  const auto prev_count = _suspend_count.fetch_sub(1, std::memory_order::release);
  ASYNC_CORO_ASSERT(prev_count != 0);

  if (prev_count == 1) {
    (void)_suspend_count.load(std::memory_order::acquire);

    if (run_data) {
      // return flag as continue execution may throw exception that should be handled in finalizer
      run_data->external_continuation_request = false;
      _handle->_run_data.store(run_data, std::memory_order::release);
    }

    // reset our cancel
    if (auto on_cancel = _handle->_on_cancel.exchange(nullptr, std::memory_order::relaxed)) {
      on_cancel->destroy();
    }

    if (_handle->is_finished()) [[unlikely]] {
      // some exception happened before callback call
      return;
    }

    _handle->get_scheduler().continue_execution(*_handle, internal::passkey{this});
  }
}

}  // namespace async_coro
