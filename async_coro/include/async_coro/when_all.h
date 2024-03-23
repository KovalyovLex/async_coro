#pragma once

#include <async_coro/internal/await_when_all.h>

namespace async_coro {

// Suspends coroutine till all tasks have not finished. Returns tuple of their results.
template <typename... TArgs>
auto when_all(task_handle<TArgs>... coroutines) {
  return internal::await_when_all(std::move(coroutines)...);
}

}  // namespace async_coro
