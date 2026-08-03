#pragma once

// NOLINTBEGIN(*macro-usage)

#include <cstdint>

#ifdef _WIN32

#define WIN_SOCKET 1
#define EPOLL_SOCKET 0
#define KQUEUE_SOCKET 0
#define EPOLL_KQUEUE_ENABLED 0

#else  // _WIN32

#define WIN_SOCKET 0

#if __linux__

#define EPOLL_SOCKET 1
#define KQUEUE_SOCKET 0
#define EPOLL_KQUEUE_ENABLED 1

#elif __APPLE__ || __FreeBSD__

#define EPOLL_SOCKET 0
#define KQUEUE_SOCKET 1
#define EPOLL_KQUEUE_ENABLED 1

#else  // __APPLE__ || __FreeBSD__

#define EPOLL_SOCKET 0
#define KQUEUE_SOCKET 0
#define EPOLL_KQUEUE_ENABLED 0

#endif  // __APPLE__ || __FreeBSD__

#endif  // _WIN32

#if WIN_SOCKET
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <WinSock2.h>
#endif

// NOLINTEND(*macro-usage)

namespace server::io {

#if WIN_SOCKET

using socket_type = SOCKET;
using file_handle_t = HANDLE;

static constexpr socket_type invalid_socket_id = INVALID_SOCKET;
static const file_handle_t invalid_file_handle = INVALID_HANDLE_VALUE;

static_assert(sizeof(socket_type) == sizeof(file_handle_t), "Wrong platform/SDK?");

#else

using socket_type = int;
using file_handle_t = int;

static constexpr socket_type invalid_socket_id = -1;
static constexpr file_handle_t invalid_file_handle = -1;

#endif

/**
 * @brief Closes a socket descriptor.
 *
 * Closes the given socket and releases the associated resource.
 *
 * @param socket_id The socket to close.
 * @return true if the socket was closed successfully or was already invalid.
 * @return false if the close operation failed.
 */
bool close_socket(socket_type socket_id) noexcept;

/**
 * @brief Closes a file handle/descriptor.
 *
 * Closes the given file handle and releases the associated resource.
 *
 * @param handle The file handle to close.
 * @return true if the handle was closed successfully or was already invalid.
 * @return false if the close operation failed.
 */
bool close_file(file_handle_t handle) noexcept;

}  // namespace server::io
