#pragma once

// NOLINTBEGIN(*macro-usage)

#include <cstdint>
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
#define NOMINMAX
#include <WinSock2.h>
#endif

// NOLINTEND(*macro-usage)

namespace server::io {

#if WIN_SOCKET

using socket_type = SOCKET;
using epoll_handle_t = HANDLE;
using file_handle_t = HANDLE;

static constexpr socket_type invalid_socket_id = INVALID_SOCKET;
static const epoll_handle_t invalid_epoll_handle = INVALID_HANDLE_VALUE;
static const file_handle_t invalid_file_handle = INVALID_HANDLE_VALUE;

static_assert(sizeof(socket_type) == sizeof(file_handle_t), "Wring platform, SDK?");

#else

using socket_type = int;
using epoll_handle_t = int;
using file_handle_t = int;

static constexpr socket_type invalid_socket_id = -1;
static constexpr epoll_handle_t invalid_epoll_handle = -1;
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
 * @brief Closes an epoll/kqueue file descriptor (or Windows HANDLE on non-Windows).
 *
 * Closes the given event-loop handle and releases the associated resource.
 *
 * @param handle The epoll/kqueue handle to close.
 * @return true if the handle was closed successfully or was already invalid.
 * @return false if the close operation failed.
 */
bool close_epoll(epoll_handle_t handle) noexcept;

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
