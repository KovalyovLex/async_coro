#include <server/io/iocp/winsock_init.h>

#if WIN_IOCP_ENABLED

#include <server/core/error.h>
#include <server/io/io_config.h>
#include <server/utils/expected.h>

// WinSock2 headers for socket I/O.
#include <mswsock.h>
#include <ws2tcpip.h>

namespace server::io {

const expected<winsock_extensions, core::error>& init_winsock() noexcept {
  static const auto cached = []() noexcept -> expected<winsock_extensions, core::error> {
    WSADATA wsa_data;
    WORD version = MAKEWORD(2, 2);
    const auto wsa_error = WSAStartup(version, &wsa_data);

    if (wsa_error != 0) {
      return expected<winsock_extensions, core::error>{
          unexpect, core::error_type::winsock_startup_failed, static_cast<int>(wsa_error)};
    }

    winsock_extensions ext{};

    // Query AcceptEx.
    {
      SOCKET tmp = socket(AF_INET, SOCK_STREAM, 0);
      if (tmp == INVALID_SOCKET) {
        return expected<winsock_extensions, core::error>{
            unexpect, core::error_type::temp_socket_creation_failed};
      }

      GUID guid = WSAID_ACCEPTEX;
      DWORD bytes = 0;
      auto err = WSAIoctl(tmp, SIO_GET_EXTENSION_FUNCTION_POINTER, &guid, sizeof(guid),
                          &ext.accept_ex, sizeof(ext.accept_ex), &bytes, nullptr, nullptr);

      if (err != 0) {
        close_socket(tmp);
        return expected<winsock_extensions, core::error>{
            unexpect, core::error_type::accept_ex_query_failed, static_cast<int>(WSAGetLastError())};
      }

      guid = WSAID_CONNECTEX;
      bytes = 0;
      err = WSAIoctl(tmp, SIO_GET_EXTENSION_FUNCTION_POINTER, &guid, sizeof(guid),
                     &ext.connect_ex, sizeof(ext.connect_ex), &bytes, nullptr, nullptr);

      if (err != 0) {
        close_socket(tmp);
        return expected<winsock_extensions, core::error>{
            unexpect, core::error_type::connect_ex_query_failed, static_cast<int>(WSAGetLastError())};
      }

      close_socket(tmp);
    }

    return ext;
  }();

  return cached;
}

}  // namespace server::io

#endif  // WIN_IOCP_ENABLED
