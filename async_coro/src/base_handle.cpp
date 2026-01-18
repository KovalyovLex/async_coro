#include <async_coro/base_handle.h>
#include <async_coro/config.h>
#include <async_coro/internal/base_handle_ptr.h>
#include <async_coro/internal/scheduled_run_data.h>
#include <async_coro/scheduler.h>
#include <async_coro/utils/passkey.h>

#include <atomic>
#include <thread>

namespace async_coro {

base_handle::~base_handle() noexcept {
  ASYNC_CORO_ASSERT(is_embedded() || _root_state.continuation.release() == nullptr);
  ASYNC_CORO_ASSERT(!_on_cancel);
}

void base_handle::set_owning_by_task(bool owning) {
  if (owning) {
    inc_num_owners();
  } else {
    dec_num_owners();
  }
}

void base_handle::set_owning_by_task_handle(bool owning) {
  ASYNC_CORO_ASSERT(!is_coro_embedded());

  set_owning_by_task(owning);
}

void base_handle::set_continuation_functor(callback_base_ptr<false> func) noexcept {
  ASYNC_CORO_ASSERT(!is_embedded());

  _root_state.continuation.reset(func.release(), std::memory_order::release);
}

void base_handle::destroy_impl() noexcept {
  callback_base_ptr<false> continuation;
  callback_base_ptr<false> start_function;

  if (!is_embedded()) {
    continuation.reset(_root_state.continuation.release(std::memory_order::acquire));
    start_function = std::move(_root_state.start_function);
  }

  // continuation can hold something from coro. So destroy continuation first
  continuation.reset();

  while (_run_data.load(std::memory_order::relaxed) != nullptr) {
    // wait for continuation finish
  }

  get_handle().destroy();
}

void base_handle::dec_num_owners() noexcept {
  if (_num_owners.fetch_sub(1, std::memory_order::release) == 1) {
    if (_num_owners.load(std::memory_order::acquire) == 0) {
      destroy_impl();
    }
  }
}

void base_handle::inc_num_owners() noexcept {
  _num_owners.fetch_add(1, std::memory_order::relaxed);
}

base_handle_ptr base_handle::get_owning_ptr() {
  return base_handle_ptr{this};
}

bool base_handle::request_cancel() {
  if (is_cancelled()) {
    // was cancelled already
    return false;
  }

  const auto [was_requested, state] = set_cancel_requested();
  if (!was_requested && (state == coroutine_state::suspended || state == coroutine_state::waiting_switch)) {
    // this is first cancel - notify continuation if coroutine was not running

    set_is_inside_cancel(true);

    if (!is_current_thread_same()) {
      while (_run_data.load(std::memory_order::acquire) != nullptr) {
        // wait for current run finish
      }
    }

    if (_current_child != nullptr) {
      _current_child->request_cancel();
    }

    _on_cancel.try_execute_and_destroy();

    execute_continuation(true);

    set_is_inside_cancel(false);
  }

  return was_requested;
}

void base_handle::continue_after_sleep() {
  // reset our cancel
  _on_cancel = cancel_callback_ptr{nullptr};

  ASYNC_CORO_ASSERT(is_cancelled() || get_scheduler().get_execution_system().is_current_thread_fits(_execution_queue));

  _execution_thread = std::this_thread::get_id();
  get_scheduler().continue_execution(*this, passkey{this});
}

}  // namespace async_coro
