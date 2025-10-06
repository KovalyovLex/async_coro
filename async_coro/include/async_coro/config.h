#pragma once

#if __cpp_exceptions == 199711L
#define ASYNC_CORO_COMPILE_WITH_EXCEPTIONS 1
#else
#define ASYNC_CORO_COMPILE_WITH_EXCEPTIONS 0
#endif

#ifndef ASYNC_CORO_WITH_EXCEPTIONS
#define ASYNC_CORO_WITH_EXCEPTIONS ASYNC_CORO_COMPILE_WITH_EXCEPTIONS
#endif

#ifndef ASYNC_CORO_ASSERT

#ifdef NDEBUG

#define ASYNC_CORO_ASSERT(x)
#define ASYNC_CORO_ASSERT_ENABLED 0
#define ASYNC_CORO_ASSERT_VARIABLE [[maybe_unused]]

#else

#include <cassert>

#define ASYNC_CORO_ASSERT(x) assert(x)
#define ASYNC_CORO_ASSERT_ENABLED 1
#define ASYNC_CORO_ASSERT_VARIABLE

#endif  // def NDEBUG

#endif  // !def ASYNC_CORO_ASSERT
