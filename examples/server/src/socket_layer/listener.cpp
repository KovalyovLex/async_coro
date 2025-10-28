#include <async_coro/config.h>
#include <server/socket_layer/connection_id.h>
#include <server/socket_layer/listener.h>
#include <server/socket_layer/socket_config.h>

#include <cerrno>
#include <charconv>
#include <cstring>
#include <ios>
#include <span>
#include <string>
#include <string_view>

#if WIN_SOCKET
#include <WS2tcpip.h>
#include <wepoll.h>

#include <iostream>
#include <stdexcept>
#else

#if EPOLL_SOCKET
#include <sys/epoll.h>
#elif KQUEUE_SOCKET
#include <sys/event.h>
#endif

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <unistd.h>

#endif

namespace server::socket_layer {

#if WIN_SOCKET
enum class socket_init_state : uint8_t {
  not_initialized,
  initialized,
  fatal_error
};

static auto win_sock_init_state = socket_init_state::not_initialized;  // NOLINT(*-non-const-*)
#endif

static bool set_non_blocking_mode(socket_type sock, std::string* error_message) {
#if WIN_SOCKET
  u_long set_on = 1;
  auto ret = ::ioctlsocket(sock, (long)FIONBIO, &set_on);
  if (ret) {
    if (error_message != nullptr) {
      *error_message = "Cannot set socket to non blocking mode with ioctlsocket: ";
      error_message->append(std::to_tring(WSAGetLastError()));
    }
    return false;
  }
#else
  const int flags = ::fcntl(sock, F_GETFL, 0);
  const auto ret = ::fcntl(sock, F_SETFL, flags | O_NONBLOCK);  // NOLINT(*-signed*, *vararg*)
  if (ret < 0) {
    if (error_message != nullptr) {
      *error_message = "Could not set socket to non blocking mode: ";
      error_message->append(strerror(errno));
    }
    return false;
  }
#endif

  return true;
}

static socket_type open_socket_impl(const std::string& ip_address, uint16_t port, bool non_block, std::string* error_message) {  // NOLINT(*-complexity*)
#if WIN_SOCKET
  if (win_sock_init_state == socket_init_state::fatal_error) {
    if (error_message != nullptr) {
      *error_message = "WSAStartup failed";
    }
    return invalid_socket_id;
  }
  ASYNC_CORO_ASSERT(win_sock_init_state == socket_init_state::initialized);
#endif

  socket_type listen_socket = invalid_socket_id;

  if (ip_address.empty()) {
    // starts listening on port

    std::array<char, 6> port_str{};  // NOLINT(*-magic-*)
    const auto res = std::to_chars(port_str.begin(), port_str.end(), port);
    if (res.ec != std::errc()) {
      if (error_message != nullptr) {
        *error_message = std::make_error_code(res.ec).message();
      }
      return invalid_socket_id;
    }
    if (res.ptr != port_str.end()) {
      *res.ptr = '\0';
    } else {
      ASYNC_CORO_ASSERT(false && "To small buffer");  // NOLINT(*-static-*)
      return invalid_socket_id;
    }

    struct free_addr_info_raii {  // NOLINT(*-special-member-*)
      addrinfo* result = nullptr;

      ~free_addr_info_raii() {
        if (result != nullptr) {
          ::freeaddrinfo(result);
        }
      }
    };

    struct close_sock_raii {  // NOLINT(*-special-member-*)
      socket_type socket_id;

      ~close_sock_raii() {
        close_socket(socket_id);
      }
    };

    addrinfo hints{};
    free_addr_info_raii addr;

    // On windows, setting up the dual-stack mode (ipv4/ipv6 on the same socket).
    // https://docs.microsoft.com/en-us/windows/win32/winsock/dual-stack-sockets
    hints.ai_family = AF_INET6;
    hints.ai_socktype = SOCK_STREAM; /* We want a TCP socket */
    hints.ai_flags = AI_PASSIVE;     /* All interfaces */

    const auto get_addr_res = ::getaddrinfo(nullptr, port_str.data(), &hints, &addr.result);
    if (get_addr_res != 0) {
      if (error_message != nullptr) {
        *error_message = "getaddrinfo: ";
        error_message->append(::gai_strerror(get_addr_res));
      }
      return invalid_socket_id;
    }

    addrinfo* result_bind = addr.result;
    for (; result_bind != nullptr; result_bind = result_bind->ai_next) {
      auto sfd = ::socket(result_bind->ai_family, result_bind->ai_socktype, result_bind->ai_protocol);
      if (sfd == -1) {
        continue;
      }

      close_sock_raii late_close{sfd};

      // Turn of IPV6_V6ONLY to accept ipv4.
      // https://stackoverflow.com/questions/1618240/how-to-support-both-ipv4-and-ipv6-connections
      int ipv6only = 0;
      if (::setsockopt(sfd, IPPROTO_IPV6, IPV6_V6ONLY, &ipv6only, sizeof(ipv6only)) != 0) {
        if (error_message != nullptr) {
          *error_message = "FATAL ERROR: setsockopt error when setting IPV6_V6ONLY to 0: ";
          error_message->append(strerror(errno));
        }
        return invalid_socket_id;
      }

      const auto bind_res = ::bind(sfd, result_bind->ai_addr, int(result_bind->ai_addrlen));
      if (bind_res == 0) {
        /* We managed to bind successfully! */
        late_close.socket_id = invalid_socket_id;
        listen_socket = sfd;
        break;
      }
    }

    if (result_bind == nullptr) {
      if (error_message != nullptr) {
        *error_message = "Could not bind to port: ";
        error_message->append(port_str.data());
        error_message->append(", error: ");
        error_message->append(strerror(errno));
      }
      close_socket(listen_socket);
      return invalid_socket_id;
    }
  } else {
    // ip address provided
    listen_socket = ::socket(AF_INET, SOCK_STREAM, 0);

    // Use the user specified IP address
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = ::inet_addr(ip_address.c_str());
    addr.sin_port = htons(port);

    const auto bind_res = ::bind(listen_socket, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));  // NOLINT(*-reinterpret-cast)
    if (bind_res != 0) {
      if (error_message != nullptr) {
        *error_message = "Could not bind to: ";
        error_message->append(ip_address);
        error_message->append(", error: ");
        error_message->append(strerror(errno));
      }
      close_socket(listen_socket);
      return invalid_socket_id;
    }
  }

  if (non_block) {
    if (!set_non_blocking_mode(listen_socket, error_message)) {
      close_socket(listen_socket);
      return invalid_socket_id;
    }
  }

  const auto listen_res = ::listen(listen_socket, SOMAXCONN);
  if (listen_res < 0) {
    if (error_message != nullptr) {
      *error_message = "Could not listen socket: ";
      error_message->append(strerror(errno));
    }
    close_socket(listen_socket);
    return invalid_socket_id;
  }

  return listen_socket;
}

listener::listener() {
#if WIN_SOCKET
  if (win_sock_init_state == socket_init_state::not_initialized) {
    struct socket_initializer {
      socket_initializer() {
        // Start the winsock DLL
        WSADATA wsaData;
        WORD wVersionRequested = MAKEWORD(2, 2);
        const auto err = WSAStartup(wVersionRequested, &wsaData);
        if (err != 0) {
          win_sock_init_state = socket_init_state::fatal_error;

#if ASYNC_CORO_COMPILE_WITH_EXCEPTIONS
          throw std::runtime_error(std::string{"WSAStartup failed with error: "} + std::to_string(err));
#else
          std::cerr << "WSAStartup failed with error: " << err << std::endl;
          return;
#endif
        }

        win_sock_init_state = socket_init_state::initialized;
      }
    };

    static auto initializer = socket_initializer{};
  }
#endif
}

bool listener::open(const std::string& ip_address, uint16_t port, std::string* error_message) {
  ASYNC_CORO_ASSERT(_opened_connection == invalid_connection);

  auto socket = open_socket_impl(ip_address, port, true, error_message);

  if (socket == invalid_socket_id) {
    return false;
  }

  _opened_connection = connection_id{socket};

  return true;
}

listener::connection_result listener::process_loop(std::span<char>* host_name_buffer, std::span<char>* service_name_buffer) {
  ASYNC_CORO_ASSERT(_opened_connection != invalid_connection);

  sockaddr_storage in_addr_storage;  // NOLINT(*member-init*)
  socklen_t in_len = sizeof(in_addr_storage);

  while (true) {
    const auto accept_sock = ::accept(_opened_connection.get_platform_id(), reinterpret_cast<sockaddr*>(&in_addr_storage), &in_len);  // NOLINT(*-reinterpret-cast)
    if (accept_sock == invalid_socket_id) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        return {.connection = invalid_connection, .type = listen_result_type::wait_for_connections};
      }
      continue;
    }

    // set non blocking mode in case of error just silently close the socket
    if (!set_non_blocking_mode(accept_sock, nullptr)) {
      close_socket(accept_sock);
      continue;
    }

    connection_result result{.connection = connection_id{accept_sock}, .type = listen_result_type::connected};

    if (host_name_buffer != nullptr && service_name_buffer != nullptr) {
      const auto name_info_res = ::getnameinfo(reinterpret_cast<sockaddr*>(&in_addr_storage), sizeof(in_addr_storage), host_name_buffer->data(), host_name_buffer->size(), service_name_buffer->data(), service_name_buffer->size(), 0);  // NOLINT(*-reinterpret-cast)

      if (name_info_res == 0) {
        result.host_name = {host_name_buffer->data()};
        result.service_name = {service_name_buffer->data()};
      }
    } else if (host_name_buffer != nullptr) {
      const auto name_info_res = ::getnameinfo(reinterpret_cast<sockaddr*>(&in_addr_storage), sizeof(in_addr_storage), host_name_buffer->data(), host_name_buffer->size(), nullptr, 0, 0);  // NOLINT(*-reinterpret-cast)

      if (name_info_res == 0) {
        result.host_name = {host_name_buffer->data()};
      }
    } else if (service_name_buffer != nullptr) {
      const auto name_info_res = ::getnameinfo(reinterpret_cast<sockaddr*>(&in_addr_storage), sizeof(in_addr_storage), nullptr, 0, service_name_buffer->data(), service_name_buffer->size(), 0);  // NOLINT(*-reinterpret-cast)

      if (name_info_res == 0) {
        result.service_name = {service_name_buffer->data()};
      }
    }

    return result;
  }
}

listener::~listener() {
  if (_opened_connection != invalid_connection) {
    close_socket(_opened_connection.get_platform_id());
  }
}

}  // namespace server::socket_layer
