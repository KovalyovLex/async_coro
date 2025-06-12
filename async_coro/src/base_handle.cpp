#include <async_coro/base_handle.h>
#include <async_coro/callback.h>
#include <async_coro/config.h>

namespace async_coro {

base_handle::~base_handle() noexcept {
  ASYNC_CORO_ASSERT(!get_has_continuation());
}

void base_handle::on_task_freed_by_scheduler() {
  if (is_coro_embedded() || !get_has_handle()) {
    destroy_impl();
  } else {
    set_ready_for_destroy();
  }
}

void base_handle::set_owning_by_task_handle(bool owning) {
  ASYNC_CORO_ASSERT(!is_coro_embedded());

  set_has_handle(owning);
  if (!owning) {
    try_destroy_if_ready();
  }
}

void base_handle::set_continuation_functor(callback_base* f) noexcept {
  ASYNC_CORO_ASSERT(!is_embedded());
  ASYNC_CORO_ASSERT(!get_has_continuation());

  _continuation.store(f, std::memory_order::release);
  set_has_continuation(true);
}

void base_handle::try_destroy_if_ready() {
  uint8_t expected_state = _atomic_state.load(std::memory_order::acquire);
  if ((expected_state & ready_for_destroy_mask) == 0) {
    // was not ready for destroy
    return;
  }

  bool can_destroy = true;
  while (!_atomic_state.compare_exchange_strong(expected_state, expected_state & ~ready_for_destroy_mask, std::memory_order::relaxed)) {
    if ((expected_state & ready_for_destroy_mask) == 0) {
      // was not ready for destroy
      can_destroy = false;
      break;
    }
  }

  if (can_destroy) {
    destroy_impl();
  }
}

void base_handle::destroy_impl() {
  auto continuation = get_continuation_functor();

  _continuation.store(nullptr, std::memory_order::relaxed);
  set_has_continuation(false);

  const auto handle = std::exchange(_handle, {});
  handle.destroy();

  if (continuation) {
    continuation->destroy();
  }
}

}  // namespace async_coro