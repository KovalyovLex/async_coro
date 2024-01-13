#pragma once

#include <async_coro/config.h>
#include <async_coro/internal/promise_result.h>
#include <async_coro/internal/passkey.h>
#include <async_coro/base_promise.h>
#include <coroutine>
#include <concepts>
#include <utility>

namespace async_coro
{
	template <typename R>
	struct promise;

	template <typename R>
	struct promise_type final : internal::promise_result<R>, base_promise
	{
		// construct my promise from me
		constexpr auto get_return_object() noexcept { return std::coroutine_handle<promise_type>::from_promise(*this); }

		// all promises awaits to be started in scheduller or after embedding
		constexpr auto initial_suspend() noexcept {
			return std::suspend_always();
		};

		// resume parent routine
		constexpr auto final_suspend() noexcept {
			return std::suspend_always();
		};

		template<typename T>
		constexpr decltype(auto) await_transform(T&& in) {
			// return non standart awaiters as is
			return std::move(in);
		}

		template<typename T>
		constexpr decltype(auto) await_transform(promise<T>&& in) {
			// start execution of internal coroutine
			in.resume(internal::passkey { this });
			return std::move(in);
		}
	};

	// Default type for all coroutines
	template <typename R>
	struct promise final
	{
		using promise_type = async_coro::promise_type<R>;
		using handle_type = std::coroutine_handle<promise_type>;
		using return_type = R;

		promise(handle_type h) noexcept
			: _handle(std::move(h))
		{ }

		promise(const promise&) = delete;
		promise(promise&& other) noexcept
			: _handle(std::exchange(other._handle, nullptr))
		{ }

		promise& operator=(const promise&) = delete;
		promise& operator=(promise&& other) noexcept {
			std::swap(_handle, other._handle);
			return *this;
		}

		~promise() noexcept {
			if (_handle) {
				_handle.destroy();
			}
		}

		struct awaiter {
			promise& t;

			bool await_ready() const noexcept { return t._handle.done(); }

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

		// coroutines can only be started and executed in scheduler or can be continued after finish of embedded coro
		void resume(internal::passkey_successors<base_promise>) {
			_handle.resume();
		}

	private:
		handle_type _handle {};
	};
}
