#pragma once

#include <async_coro/config.h>
#include <async_coro/internal/promise_result.h>
#include <async_coro/internal/passkey.h>
#include <async_coro/base_handle.h>
#include <coroutine>
#include <concepts>
#include <utility>

namespace async_coro
{
	template<typename R>
	struct task;

	class scheduler;

	template<typename R>
	struct promise_type final : internal::promise_result<R>, base_handle
	{
		// construct my promise from me
		constexpr auto get_return_object() noexcept { return std::coroutine_handle<promise_type>::from_promise(*this); }

		// all promises awaits to be started in scheduller or after embedding
		constexpr auto initial_suspend() noexcept {
			init_promise(get_return_object());
			return std::suspend_always();
		}

		// resume parent routine
		constexpr auto final_suspend() noexcept {
			return std::suspend_always();
		}

		template<typename T>
		constexpr decltype(auto) await_transform(T&& in) {
			// return non standart awaiters as is
			return std::move(in);
		}

		template<typename T>
		constexpr decltype(auto) await_transform(task<T>&& in) {
			auto handle = in.get_handle(internal::passkey { this });
			get_scheduler().on_child_coro_added(*this, handle.promise());
			return std::move(in);
		}
	};

	// Default type for all coroutines
	template<typename R>
	struct task final
	{
		using promise_type = async_coro::promise_type<R>;
		using handle_type = std::coroutine_handle<promise_type>;
		using return_type = R;

		task(handle_type h) noexcept
			: _handle(std::move(h))
		{ }

		task(const task&) = delete;
		task(task&& other) noexcept
			: _handle(std::exchange(other._handle, nullptr))
		{ }

		task& operator=(const task&) = delete;
		task& operator=(task&& other) noexcept {
			std::swap(_handle, other._handle);
			return *this;
		}

		~task() noexcept {
			if (_handle) {
				_handle.destroy();
			}
		}

		struct awaiter {
			task& t;

			bool await_ready() const noexcept {
				return t._handle.done();
			}

			template <typename T>
			void await_suspend(std::coroutine_handle<T>) const noexcept { }

			decltype(auto) await_resume() const {
				return t._handle.promise().move_result();
			}
		};

		// coroutine should be moved to become embedded
		auto operator co_await() && {
			return awaiter(*this);
		}

		auto operator co_await() & = delete;
		auto operator co_await() const & = delete;
		auto operator co_await() const && = delete;

		bool done() const {
			return _handle.done();
		}

		template<typename T>
		handle_type get_handle(internal::passkey<async_coro::promise_type<T>>) {
			return _handle;
		}

		handle_type release_handle(internal::passkey_successors<scheduler>) {
			return std::exchange(_handle, {});
		}

	private:
		handle_type _handle {};
	};
}
