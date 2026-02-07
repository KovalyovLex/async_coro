#include <async_coro/base_handle.h>
#include <async_coro/config.h>
#include <async_coro/executor_data.h>
#include <async_coro/internal/base_handle_ptr.h>
#include <async_coro/internal/scheduled_run_data.h>
#include <async_coro/scheduler.h>
#include <async_coro/utils/passkey.h>

#include <atomic>
#include <thread>
#include <utility>

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

  // Use barrier here as cancel or update can change state
  while (_run_data.load(std::memory_order::acquire) != nullptr) {
    // wait for continuation finish
  }

  // Synchronizing cancel state (set_cancel_requested releases value)
  (void)get_coroutine_state(std::memory_order::acquire);

  get_handle().destroy();
}

void base_handle::dec_num_owners() noexcept {
  if (_num_owners.fetch_sub(1, std::memory_order::acq_rel) == 1) {
    destroy_impl();
  }
}

void base_handle::inc_num_owners() noexcept {
  _num_owners.fetch_add(1, std::memory_order::relaxed);
}

base_handle_ptr base_handle::get_owning_ptr() {
  return base_handle_ptr{this};
}

base_handle::defer_leave base_handle::enter_update_loop(internal::scheduled_run_data& run_data, internal::scheduled_run_data*& current_data, std::thread::id current_thread) noexcept {
  if (is_execution_thread_same(current_thread)) {
    current_data = _run_data.load(std::memory_order::relaxed);
    if (current_data != nullptr && is_execution_thread_same(current_thread)) {
      // we are in update loop. Its save to continue execution
      current_data = _run_data.load(std::memory_order::relaxed);
      ASYNC_CORO_ASSERT(current_data != nullptr);
      return {this, false};
    }
  }

  // enter crit section exclusively

  current_data = nullptr;
  while (!_run_data.compare_exchange_weak(current_data, std::addressof(run_data), std::memory_order::relaxed)) {
    // wait while we set _run_data exclusively
    current_data = nullptr;
  }
  current_data = std::addressof(run_data);

  if (!is_execution_thread_same(current_thread)) {
    _execution_thread.store(current_thread, std::memory_order::relaxed);
    current_data = _run_data.load(std::memory_order::acquire);  // to sync data with another thread
  }

  return {this, true};
}

void base_handle::leave_update_loop() noexcept {
  _run_data.store(nullptr, std::memory_order::release);
}

bool base_handle::request_cancel() {
  if (is_cancelled()) {
    // was cancelled already
    return false;
  }

  const auto [was_requested, state] = set_cancel_requested();
  if (!was_requested && (state == coroutine_state::suspended || state == coroutine_state::waiting_switch)) {
    // this is first cancel - notify continuation if coroutine was not running

    internal::scheduled_run_data run_data{};
    internal::scheduled_run_data* current_data = nullptr;
    auto current_thread = std::this_thread::get_id();

    auto leave = enter_update_loop(run_data, current_data, current_thread);

    if (_current_child != nullptr) {
      _current_child->request_cancel();
    }

    _on_cancel.try_execute_and_destroy();

    execute_continuation(true);

    ASYNC_CORO_ASSERT(run_data.coroutine_to_run_next == nullptr || run_data.coroutine_to_run_next->is_cancelled());
  }

  return was_requested;
}

void base_handle::continue_after_sleep(std::thread::id current_thread) {
  // reset our cancel
  _on_cancel = cancel_callback_ptr{nullptr};

  ASYNC_CORO_ASSERT(is_cancelled() || get_scheduler().get_execution_system().is_thread_fits(_execution_queue, current_thread));

  get_scheduler().continue_execution(*this, current_thread, passkey{this});
}

}  // namespace async_coro
