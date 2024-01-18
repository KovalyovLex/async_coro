#pragma once

#include <async_coro/base_handle.h>
#include <concepts>
#include <coroutine>

namespace async_coro::internal
{
	template<typename T>
	struct await_callback_awaitable
	{
		T _on_await;

		await_callback_awaitable(T&& on_await) 
			: _on_await(std::move(on_await))
		{ }
		await_callback_awaitable(const await_callback_awaitable&) = delete;
		await_callback_awaitable(await_callback_awaitable&&) = delete;

		await_callback_awaitable& operator=(await_callback_awaitable&&) = delete;
		await_callback_awaitable& operator=(const await_callback_awaitable&) = delete;

		bool await_ready() { return false; }

		template<typename U> requires(std::derived_from<U, base_handle>)
		void await_suspend(std::coroutine_handle<U> h)
		{
			_on_await([h]() {
				h.resume();
			});
		}

		void await_resume() { }
	};

	template<typename T> await_callback_awaitable(T&&) -> await_callback_awaitable<T>;
}
