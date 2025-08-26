#pragma once

#if defined _MSC_VER

#define ASYNC_CORO_WARNINGS_MSVC_PUSH __pragma(warning(push))
#define ASYNC_CORO_WARNINGS_MSVC_POP __pragma(warning(pop))

#define ASYNC_CORO_WARNINGS_PUSH ASYNC_CORO_WARNINGS_MSVC_PUSH
#define ASYNC_CORO_WARNINGS_POP ASYNC_CORO_WARNINGS_MSVC_POP

#define ASYNC_CORO_WARNINGS_MSVC_IGNORE(warn_num) __pragma(warning(disable : warn_num))

#define ASYNC_CORO_WARNINGS_GCC_IGNORE(mac_name)

#elif defined(__clang__) || defined(__GNUC__)

#define ASYNC_CORO_WARNINGS_GCC_PUSH _Pragma("clang diagnostic push")
#define ASYNC_CORO_WARNINGS_GCC_POP _Pragma("clang diagnostic pop")

#define ASYNC_CORO_WARNINGS_PUSH ASYNC_CORO_WARNINGS_GCC_PUSH
#define ASYNC_CORO_WARNINGS_POP ASYNC_CORO_WARNINGS_GCC_POP

#define ASYNC_CORO_WARNINGS_GCC_IGNORE_0(mac_name) #mac_name
#define ASYNC_CORO_WARNINGS_GCC_IGNORE_1(mac_name) ASYNC_CORO_WARNINGS_GCC_IGNORE_0(clang diagnostic ignored "-W" #mac_name)
#define ASYNC_CORO_WARNINGS_GCC_IGNORE(mac_name) _Pragma(ASYNC_CORO_WARNINGS_GCC_IGNORE_1(mac_name))

#define ASYNC_CORO_WARNINGS_MSVC_IGNORE(warn_num)
#define ASYNC_CORO_WARNINGS_MSVC_PUSH
#define ASYNC_CORO_WARNINGS_MSVC_POP

#endif
