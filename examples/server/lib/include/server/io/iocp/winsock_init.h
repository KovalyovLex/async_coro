#pragma once

#if WIN_IOCP_ENABLED

#include <server/core/error.h>
#include <server/io/io_config.h>
#include <server/utils/expected.h>

#include <cstdint>

namespace server::io {

/**
 * @brief WinSock extension function pointers for AcceptEx / ConnectEx.
 *
 * These functions are deprecated from the public WinSock2 API and must be
 * queried at runtime via WSAIoctl(SIO_GET_EXTENSION_FUNCTION_POINTER).
 */
struct winsock_extensions {
  using LPFN_ACCEPTEX = BOOL(WSAAPI*)(SOCKET, SOCKET, PVOID, DWORD, DWORD, DWORD, LPDWORD, LPOVERLAPPED);
  using LPFN_CONNECTEX = BOOL(WSAAPI*)(SOCKET, const sockaddr*, int, PVOID, DWORD, LPDWORD, LPOVERLAPPED);

  LPFN_ACCEPTEX accept_ex = nullptr;
  LPFN_CONNECTEX connect_ex = nullptr;
};

/**
 * @brief Initialize WinSock and query extension function pointers.
 *
 * Performs WSAStartup(2.2) on first call (lazy, thread-safe via C++11 static init).
 * Queries AcceptEx and ConnectEx function pointers using a temporary socket.
 * Subsequent calls return the cached result immediately.
 *
 * @return An expected<winsock_extensions, core::error>. On success, contains the
 *         extension function pointers. On failure, contains an error.
 */
[[nodiscard]] const expected<winsock_extensions, core::error>& init_winsock() noexcept;

}  // namespace server::io

#endif  // WIN_IOCP_ENABLED
