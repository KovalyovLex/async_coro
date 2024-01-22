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

#if defined(_MSC_VER)
#if _MSC_VER >= 1929
// VS2019 v16.10 and later (_MSC_FULL_VER >= 192829913 for VS 2019 v16.9)
// Works with /std:c++14 and /std:c++17, and performs optimization
#define ASYNC_CORO_NO_UNIQUE_ADDRESS [[msvc::no_unique_address]]
#else
// no-op in MSVC v14x ABI
#define ASYNC_CORO_NO_UNIQUE_ADDRESS /* [[no_unique_address]] */
#endif
#else
#define ASYNC_CORO_NO_UNIQUE_ADDRESS [[no_unique_address]]
#endif
