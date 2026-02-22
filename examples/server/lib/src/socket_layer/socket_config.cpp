#include <server/socket_layer/socket_config.h>

#if !WIN_SOCKET
#include <unistd.h>
#endif

namespace server::socket_layer {

void close_socket(socket_type socket_id) noexcept {
  if (socket_id != invalid_socket_id) {
#if WIN_SOCKET
    ::closesocket(socket_id);
#else
    ::close(socket_id);
#endif
  }
}

}  // namespace server::socket_layer
