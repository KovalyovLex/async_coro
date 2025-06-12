#include <async_coro/base_handle.h>
#include <async_coro/callback.h>
#include <async_coro/config.h>

namespace async_coro {

base_handle::~base_handle() noexcept {
  ASYNC_CORO_ASSERT(!get_has_continuation());
}

void base_handle::on_task_freed_by_scheduler() {
  if (is_coro_embedded() || dec_num_owners() == 0) {
    destroy_impl();
  }
}

void base_handle::set_owning_by_task_handle(bool owning) {
  ASYNC_CORO_ASSERT(!is_coro_embedded());

  if (owning) {
    inc_num_owners();
  } else {
    if (dec_num_owners() == 0) {
      destroy_impl();
    }
  }
}

void base_handle::set_continuation_functor(callback_base* f) noexcept {
  ASYNC_CORO_ASSERT(!is_embedded());
  ASYNC_CORO_ASSERT(!get_has_continuation());

  _continuation.store(f, std::memory_order::release);
  set_has_continuation(true);
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

uint8_t base_handle::dec_num_owners() noexcept {
  constexpr auto step = uint8_t(1 << 5);

  uint8_t expected = _atomic_state.load(std::memory_order::acquire);
  uint8_t new_value = (expected - step);
  ASYNC_CORO_ASSERT(expected >= step);

  while (!_atomic_state.compare_exchange_strong(expected, new_value, std::memory_order::release, std::memory_order::acquire)) {
    new_value = (expected - step);
    ASYNC_CORO_ASSERT(expected >= step);
  }
  return (expected & num_owners_mask) >> 5;
}

uint8_t base_handle::inc_num_owners() noexcept {
  constexpr auto step = uint8_t(1 << 5);

  uint8_t expected = _atomic_state.load(std::memory_order::relaxed);
  uint8_t new_value = (expected + step);
  ASYNC_CORO_ASSERT((expected & num_owners_mask) < num_owners_mask);

  while (!_atomic_state.compare_exchange_strong(expected, new_value, std::memory_order::relaxed)) {
    new_value = (expected + step);
    ASYNC_CORO_ASSERT((expected & num_owners_mask) < num_owners_mask);
  }
  return (expected & num_owners_mask) >> 5;
}

}  // namespace async_coro