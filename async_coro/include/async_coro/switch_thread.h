#pragma once

#include <async_coro/internal/await_switch_awaitable.h>

namespace async_coro {
auto switch_thread(execution_thread thread) {
  return internal::await_switch_awaitable{thread};
}
}  // namespace async_coro
