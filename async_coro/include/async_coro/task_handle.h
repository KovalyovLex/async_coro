#pragma once

#include <async_coro/task.h>

namespace async_coro
{
	template<typename R>
	class task_handle
	{
	public:
		task_handle() noexcept = default;
		explicit task_handle(std::coroutine_handle<async_coro::promise_type<R>> h) noexcept
			: _handle(std::move(h))
		{ }

		task_handle(const task_handle&) = delete;
		task_handle(task_handle&& other) noexcept
			: _handle(std::exchange(other._handle, nullptr))
		{ }

		task_handle& operator=(const task_handle&) = delete;
		task_handle& operator=(task_handle&& other) noexcept {
			std::swap(_handle, other._handle);
			return *this;
		}

		~task_handle() noexcept {
			// task handle doesn't own task
		 }

		// access
		decltype(auto) get() & {
			return _handle.promise().get_result_ref();
		}

		decltype(auto) get() const & {
			return _handle.promise().get_result_cref();
		}

		decltype(auto) get() && {
			return _handle.promise().move_result();
		}

		void get() const && = delete;

		template <typename Y> requires(std::same_as<Y, R> && !std::same_as<R, void>)
		operator Y&() & {
			return _handle.promise().get_result_ref();
		}
		template <typename Y> requires(std::same_as<Y, R> && !std::same_as<R, void>)
		operator const Y&() const & {
			return _handle.promise().get_result_cref();
		}
		template <typename Y> requires(std::same_as<Y, R> && !std::same_as<R, void>)
		operator Y() && {
			return _handle.promise().move_result();
		}

		bool done() const {
			return _handle.done();
		}

	private:
		std::coroutine_handle<async_coro::promise_type<R>> _handle {};
	};
}
