#pragma once

#ifdef _WIN32

#define WIN_SOCKET 1
#define EPOLL_SOCKET 1
#define KQUEUE_SOCKET 0

#else  // _WIN32

#define WIN_SOCKET 0

#if __linux__

#define EPOLL_SOCKET 1
#define KQUEUE_SOCKET 0

#else  // __linux__

#define EPOLL_SOCKET 0

#if __APPLE__  // optionally __FreeBSD__

#define KQUEUE_SOCKET 1

#else  // __APPLE__
static_assert(false, "Unsupported platform");

#endif  // __APPLE__

#endif  // __linux__

#endif  // _WIN32

#if WIN_SOCKET
#define WIN32_LEAN_AND_MEAN
#include <WinSock2.h>
#endif

namespace server::socket_layer {

#if WIN_SOCKET
using socket_type = SOCKET;
using epoll_handle_t = HANDLE;
static constexpr socket_type invalid_socket_id = INVALID_SOCKET;
#else
using socket_type = int;
using epoll_handle_t = int;
static constexpr socket_type invalid_socket_id = -1;
#endif

void close_socket(socket_type socket_id) noexcept;

}  // namespace server::socket_layer
