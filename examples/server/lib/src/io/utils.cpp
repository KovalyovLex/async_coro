#include <server/io/utils.h>

#if !WIN_SOCKET
#include <unistd.h>
#endif

namespace server::io {

bool close_socket(socket_type socket_id) noexcept {
  if (socket_id != invalid_socket_id) {
#if WIN_SOCKET
    return ::closesocket(socket_id) == 0;
#else
    return ::close(socket_id) == 0;
#endif
  }
  return true;
}

bool close_file(file_handle_t handle) noexcept {
  if (handle != invalid_file_handle) {
#if WIN_SOCKET
    return ::CloseHandle(handle);
#else
    return ::close(handle) == 0;
#endif
  }
  return true;
}

}  // namespace server::io
