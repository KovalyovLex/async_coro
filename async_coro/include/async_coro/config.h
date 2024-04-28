#pragma once

#ifndef ASYNC_CORO_NO_EXCEPTIONS
#if __cpp_exceptions == 199711L
#define ASYNC_CORO_NO_EXCEPTIONS 0
#else
#define ASYNC_CORO_NO_EXCEPTIONS 1
#endif
#endif

#ifndef ASYNC_CORO_ASSERT
#include <cassert>
#define ASYNC_CORO_ASSERT assert
#endif
