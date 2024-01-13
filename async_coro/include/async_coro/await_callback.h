#pragma once

#include <async_coro/internal/await_callback_awaitable.h>
#include <utility>

namespace async_coro
{
	// Suspends coroutine and call continuation function with resume function as argument.
	// Resume function has signature void(), its invocation immedatelly resumes the coroutine.
	template<typename T>
	auto await_callback(T continuation) {
		return internal::await_callback_awaitable { std::move(continuation) };
	}
}
