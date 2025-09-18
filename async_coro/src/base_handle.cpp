#include <async_coro/base_handle.h>
#include <async_coro/callback.h>
#include <async_coro/config.h>
#include <async_coro/internal/passkey.h>
#include <async_coro/scheduler.h>

#include <atomic>

namespace async_coro {

base_handle::~base_handle() noexcept {
  ASYNC_CORO_ASSERT(release_continuation_functor() == nullptr);
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

void base_handle::set_continuation_functor(callback_base* f) noexcept {
  ASYNC_CORO_ASSERT(!is_embedded());

  auto old_value = _continuation.exchange(f, std::memory_order::release);
  if (old_value) {
    old_value->destroy();
  }
}

void base_handle::destroy_impl() {
  auto continuation = release_continuation_functor();

  get_handle().destroy();

  if (continuation) {
    continuation->destroy();
  }
}

uint8_t base_handle::dec_num_owners() noexcept {
  uint8_t expected = _atomic_state.load(std::memory_order::acquire);
  uint8_t new_value = (expected - num_owners_step);
  ASYNC_CORO_ASSERT(expected >= num_owners_step);

  while (!_atomic_state.compare_exchange_strong(expected, new_value, std::memory_order::release, std::memory_order::acquire)) {
    new_value = (expected - num_owners_step);
    ASYNC_CORO_ASSERT(expected >= num_owners_step);
  }
  return (new_value & num_owners_mask) >> 4;
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
  bool was_requested = set_cancel_requested(true);
  if (!was_requested) {
    // this is first cancel - notify continuation
    const auto state = get_coroutine_state(std::memory_order::acquire);
    if ((state == coroutine_state::suspended || state == coroutine_state::waiting_switch)) {
      if (_current_child) {
        _current_child->request_cancel();
      }
      if (_on_cancel) {
        _on_cancel->execute();
      }
    }
    execute_continuation(true);
  }
  return was_requested;
}

}  // namespace async_coro