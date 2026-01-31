#pragma once

#if defined _MSC_VER

#define ASYNC_CORO_NO_UNIQUE_ADDRESS [[msvc::no_unique_address]]

#else

#define ASYNC_CORO_NO_UNIQUE_ADDRESS [[no_unique_address]]

#endif
