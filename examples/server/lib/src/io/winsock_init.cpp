#include <server/io/winsock_init.h>

#if WIN_IOCP_ENABLED

#include <server/utils/expected.h>

// WinSock2 headers for socket I/O.
#include <mswsock.h>
#include <ws2tcpip.h>

namespace server::io {

const expected<winsock_extensions, std::string>& init_winsock() noexcept {
  static const auto cached = []() noexcept -> expected<winsock_extensions, std::string> {
    WSADATA wsa_data;
    WORD version = MAKEWORD(2, 2);
    const auto wsa_error = WSAStartup(version, &wsa_data);

    if (wsa_error != 0) {
      return expected<winsock_extensions, std::string>{
          unexpect,
          "WSAStartup failed with error: " + std::to_string(wsa_error)};
    }

    winsock_extensions ext{};

    // Query AcceptEx.
    {
      SOCKET tmp = socket(AF_INET, SOCK_STREAM, 0);
      if (tmp == INVALID_SOCKET) {
        return expected<winsock_extensions, std::string>{
            unexpect, "Failed to create temporary socket for AcceptEx query"};
      }

      GUID guid = WSAID_ACCEPTEX;
      DWORD bytes = 0;
      auto err = WSAIoctl(tmp, SIO_GET_EXTENSION_FUNCTION_POINTER, &guid, sizeof(guid),
                          &ext.accept_ex, sizeof(ext.accept_ex), &bytes, nullptr, nullptr);

      if (err != 0) {
        close_socket(tmp);
        return expected<winsock_extensions, std::string>{
            unexpect, "WSAIoctl failed to query AcceptEx: " + std::to_string(WSAGetLastError())};
      }

      guid = WSAID_CONNECTEX;
      bytes = 0;
      err = WSAIoctl(tmp, SIO_GET_EXTENSION_FUNCTION_POINTER, &guid, sizeof(guid),
                     &ext.connect_ex, sizeof(ext.connect_ex), &bytes, nullptr, nullptr);

      if (err != 0) {
        close_socket(tmp);
        return expected<winsock_extensions, std::string>{
            unexpect, "WSAIoctl failed to query ConnectEx: " + std::to_string(WSAGetLastError())};
      }

      close_socket(tmp);
    }

    return ext;
  }();

  return cached;
}

}  // namespace server::io

#endif  // WIN_IOCP_ENABLED
