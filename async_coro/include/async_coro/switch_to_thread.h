#pragma once

#include <async_coro/internal/await_switch.h>

namespace async_coro {
// Construction to use when you want to execute next part of coroutine in separae thread (main thread or one of the workers).
// Usage: await switch_thread(execution_thread::main);
// ... your code that will be executed in main thread ...
auto switch_to_thread(execution_thread thread) {
  return internal::await_switch{thread};
}
}  // namespace async_coro
