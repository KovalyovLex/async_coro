#pragma once

#include <async_coro/config.h>

#include <memory>
#include <type_traits>
#if !ASYNC_CORO_NO_EXCEPTIONS
#include <exception>
#endif

namespace async_coro::internal
{
#if ASYNC_CORO_NO_EXCEPTIONS
		template <typename T>
		union store_type {
			static inline constexpr bool nothrow_destructible = std::is_nothrow_destructible_v<T>;

			T result;

			store_type() noexcept { }
			~store_type() noexcept { }
			void destroy_exception() { }
			void destroy_result() noexcept(noexcept(std::is_nothrow_destructible_v<T>))
			{
				std::destroy_at(&result);
			}
		};

		template <>
		union store_type<void> {
			static inline constexpr bool nothrow_destructible = true;

			store_type() noexcept { }
			~store_type() noexcept { }
			void destroy_exception() noexcept { }
			void destroy_result() noexcept { }
		};
#else 
		template <typename T>
		union store_type {
			static inline constexpr bool nothrow_destructible = std::is_nothrow_destructible_v<std::exception_ptr> && std::is_nothrow_destructible_v<T>;

			std::exception_ptr exception;
			T result;

			store_type() noexcept { }
			~store_type() noexcept { }
			void destroy_exception() noexcept(std::is_nothrow_destructible_v<std::exception_ptr>) 
			{
				std::destroy_at(&exception);
			}
			void destroy_result() noexcept(noexcept(std::is_nothrow_destructible_v<T>))
			{
				std::destroy_at(&result);
			}
		};

		template <>
		union store_type<void> {
			static inline constexpr bool nothrow_destructible = std::is_nothrow_destructible_v<std::exception_ptr>;

			std::exception_ptr exception;

			store_type() noexcept { }
			~store_type() noexcept { }
			void destroy_exception() noexcept(std::is_nothrow_destructible_v<std::exception_ptr>)
			{
				std::destroy_at(&exception);
			}
			void destroy_result() noexcept { }
		};
#endif
}
