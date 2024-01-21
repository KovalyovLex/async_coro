#pragma once

#include <async_coro/base_handle.h>
#include <async_coro/scheduler.h>
#include <concepts>
#include <coroutine>

namespace async_coro::internal
{
	template<typename T>
	struct await_callback_awaitable
	{
		T _on_await;

		explicit await_callback_awaitable(T&& on_await) 
			: _on_await(std::move(on_await))
		{ }
		await_callback_awaitable(const await_callback_awaitable&) = delete;
		await_callback_awaitable(await_callback_awaitable&&) = delete;

		await_callback_awaitable& operator=(await_callback_awaitable&&) = delete;
		await_callback_awaitable& operator=(const await_callback_awaitable&) = delete;

		bool await_ready() const noexcept { return false; }

		template<typename U> requires(std::derived_from<U, base_handle>)
		void await_suspend(std::coroutine_handle<U> h)
		{
			_on_await([h, executed = false]() mutable {
				if (!executed) [[likely]] {
					executed = true;

					base_handle& handle = h.promise();
					handle.get_scheduler().continue_execution(handle);
				}
			});
		}

		void await_resume() const noexcept { }
	};

	template<typename T> await_callback_awaitable(T&&) -> await_callback_awaitable<T>;
}
