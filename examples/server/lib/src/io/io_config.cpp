#include <server/io/io_config.h>

#if !WIN_SOCKET
#include <unistd.h>
#endif

namespace server::io {

void close_socket(socket_type socket_id) noexcept {
  if (socket_id != invalid_socket_id) {
#if WIN_SOCKET
    ::closesocket(socket_id);
#else
    ::close(socket_id);
#endif
  }
}

void close_epoll(epoll_handle_t handle) noexcept {
  if (handle != invalid_epoll_handle) {
#if WIN_SOCKET
    ::CloseHandle(handle);
#else
    ::close(handle);
#endif
  }
}

void close_file(file_handle_t handle) noexcept {
  if (handle != invalid_file_handle) {
#if WIN_SOCKET
    ::CloseHandle(handle);
#else
    ::close(handle);
#endif
  }
}

}  // namespace server::io
