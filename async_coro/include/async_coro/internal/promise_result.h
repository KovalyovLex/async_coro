#pragma once

#include <async_coro/internal/store_type.h>
#include <utility>

namespace async_coro::internal
{
	template <typename T>
	struct promise_result_base
	{
		promise_result_base()
			: is_initialized(false)
			, is_result(false)
		{ }

		~promise_result_base() noexcept(store_type<T>::nothrow_destructible) {
			if (is_initialized) {
				if (is_result) {
					store.destroy_result();
				} else {
					store.destroy_exception();
				}
			}
		}

		bool has_result() const noexcept { return is_initialized && is_result; }

#if !ASYNC_CORO_NO_EXCEPTIONS
		auto unhandled_exception() noexcept {
			ASYNC_CORO_ASSERT(!is_initialized);
			new (&store.exception) std::exception_ptr(std::current_exception());
			is_initialized = true;
			is_result = false;
		}

		void check_exception() {
			if (is_initialized && !is_result) {
				std::rethrow_exception(store.exception);
			}
		}
#else
		void check_exception() noexcept { }
#endif

	protected:
		[[no_unique_address]] store_type<T> store;
		bool is_initialized : 1;
		bool is_result : 1;
	};

	template <typename T>
	struct promise_result : promise_result_base<T>
	{
		// C++ coroutine api
		void return_value(T&& r) noexcept {
			ASYNC_CORO_ASSERT(!this->is_initialized);
			new (&this->store.result) T(std::move(r));
			this->is_initialized = true;
			this->is_result = true;
		}

		// C++ coroutine api
		void return_value(const T& r) noexcept(std::is_nothrow_copy_constructible_v<T>) {
			ASYNC_CORO_ASSERT(!this->is_initialized);
			new (&this->store.result) T(r);
			this->is_initialized = true;
			this->is_result = true;
		}

		T& get_result_ref() noexcept(ASYNC_CORO_NO_EXCEPTIONS) {
			this->check_exception();
			ASYNC_CORO_ASSERT(this->has_result());
			return this->store.result;
		}

		const T& get_result_cref() const noexcept(ASYNC_CORO_NO_EXCEPTIONS) {
			this->check_exception();
			ASYNC_CORO_ASSERT(this->has_result());
			return this->store.result;
		}

		T move_result() noexcept(ASYNC_CORO_NO_EXCEPTIONS) {
			this->check_exception();
			ASYNC_CORO_ASSERT(this->has_result());
			return std::move(this->store.result);
		}
	};

	template <>
	struct promise_result<void> : promise_result_base<void>
	{
		// C++ coroutine api
		void return_void() noexcept {
			ASYNC_CORO_ASSERT(!is_initialized);
			is_initialized = true;
			is_result = true;
		}

		void get_result_ref() noexcept(ASYNC_CORO_NO_EXCEPTIONS) {
			check_exception();
		}

		void get_result_cref() noexcept(ASYNC_CORO_NO_EXCEPTIONS) {
			check_exception();
		}

		void move_result() noexcept(ASYNC_CORO_NO_EXCEPTIONS) {
			check_exception();
		}
	};
}
