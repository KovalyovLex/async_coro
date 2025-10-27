#include <async_coro/base_handle.h>
#include <async_coro/callback.h>
#include <async_coro/config.h>
#include <async_coro/scheduler.h>
#include <async_coro/utils/passkey.h>

#include <atomic>

namespace async_coro {

base_handle::~base_handle() noexcept {
  ASYNC_CORO_ASSERT(release_continuation_functor() == nullptr);
  ASYNC_CORO_ASSERT(_on_cancel.load(std::memory_order::relaxed) == nullptr);
}

void base_handle::on_task_freed_by_scheduler() {
  const auto num_owners = dec_num_owners();
  if (num_owners == 0) {
    destroy_impl();
  }
}

void base_handle::set_owning_by_task_handle(bool owning) {
  ASYNC_CORO_ASSERT(!is_coro_embedded());

  if (owning) {
    inc_num_owners();
  } else {
    const auto num_owners = dec_num_owners();
    if (num_owners == 0) {
      destroy_impl();
    }
  }
}

void base_handle::set_continuation_functor(callback_base* func) noexcept {
  ASYNC_CORO_ASSERT(!is_embedded());

  auto* old_value = _continuation.exchange(func, std::memory_order::release);
  if (old_value != nullptr) {
    old_value->destroy();
  }
}

void base_handle::destroy_impl() {
  auto* continuation = release_continuation_functor();

  // continuation can hold something from coro. So destroy continuation first
  if (continuation != nullptr) {
    continuation->destroy();
  }

  _is_inside_cancel.wait(true, std::memory_order::acquire);

  get_handle().destroy();
}

uint8_t base_handle::dec_num_owners() noexcept {
  uint8_t expected = _atomic_state.load(std::memory_order::acquire);
  uint8_t new_value = (expected - num_owners_step);
  ASYNC_CORO_ASSERT(expected >= num_owners_step);

  while (!_atomic_state.compare_exchange_strong(expected, new_value, std::memory_order::release, std::memory_order::acquire)) {
    new_value = (expected - num_owners_step);
    ASYNC_CORO_ASSERT(expected >= num_owners_step);
  }
  return (new_value & num_owners_mask) >> 4U;  // NOLINT(*-signed-bitwise)
}

void base_handle::inc_num_owners() noexcept {
  uint8_t expected = _atomic_state.load(std::memory_order::relaxed);
  uint8_t new_value = (expected + num_owners_step);
  ASYNC_CORO_ASSERT((expected & num_owners_mask) < num_owners_mask);

  while (!_atomic_state.compare_exchange_strong(expected, new_value, std::memory_order::relaxed)) {
    new_value = (expected + num_owners_step);
    ASYNC_CORO_ASSERT((expected & num_owners_mask) < num_owners_mask);
  }
}

bool base_handle::request_cancel() {
  const auto was_in_cancel = _is_inside_cancel.exchange(true, std::memory_order::acquire);

  const auto [was_requested, state] = set_cancel_requested();
  if (!was_requested) {
    // this is first cancel - notify continuation

    // sync data
    (void)get_coroutine_state(std::memory_order::acquire);

    if ((state == coroutine_state::suspended || state == coroutine_state::waiting_switch)) {
      // It should be unsafe to use this fields if state prior cancel request was suspended
      // as any continue after our request leaves this fields untouched
      if (_current_child != nullptr) {
        _current_child->request_cancel();
      }
      if (auto* on_cancel = _on_cancel.exchange(nullptr, std::memory_order::relaxed)) {
        on_cancel->execute_and_destroy();
      }
    }

    execute_continuation(true, !was_in_cancel);

    return was_requested;
  }

  if (!was_in_cancel) {
    this->release_cancel_lock();
  }
  return was_requested;
}

void base_handle::continue_after_sleep() {
  // reset our cancel
  if (auto* on_cancel = _on_cancel.exchange(nullptr, std::memory_order::relaxed)) {
    on_cancel->destroy();
  }

  ASYNC_CORO_ASSERT(is_cancelled() || get_scheduler().get_execution_system().is_current_thread_fits(_execution_queue));

  _execution_thread = std::this_thread::get_id();
  get_scheduler().continue_execution(*this, passkey{this});
}

}  // namespace async_coro
