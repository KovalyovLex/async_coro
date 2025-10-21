#include <async_coro/config.h>
#include <server/socket_layer/connection_id.h>
#include <server/socket_layer/listener.h>
#include <server/socket_layer/socket_config.h>

#include <charconv>
#include <string>

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

static void close_socket_impl(socket_type socket_id) {
  if (socket_id != invalid_socket_id) {
#if WIN_SOCKET
    ::closesocket(socket_id);
#else
    ::close(socket_id);
#endif
  }
}

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

static socket_type open_socket_impl(const std::string& ip_address, uint16_t port, std::string* error_message, bool non_block) {  // NOLINT(*-complexity*)
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
        close_socket_impl(socket_id);
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
      close_socket_impl(listen_socket);
      return invalid_socket_id;
    }
  } else {
    // ip address provided
    listen_socket = ::socket(AF_INET, SOCK_STREAM, 0);

    // Use the user specified IP address
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = ::inet_addr(ip_address.c_str());
    addr.sin_port = port;

    const auto bind_res = ::bind(listen_socket, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));  // NOLINT(*-reinterpret-cast)
    if (bind_res != 0) {
      if (error_message != nullptr) {
        *error_message = "Could not bind to: ";
        error_message->append(ip_address);
        error_message->append(", error: ");
        error_message->append(strerror(errno));
      }
      close_socket_impl(listen_socket);
      return invalid_socket_id;
    }
  }

  if (non_block) {
    if (!set_non_blocking_mode(listen_socket, error_message)) {
      close_socket_impl(listen_socket);
      return invalid_socket_id;
    }
  }

  const auto listen_res = ::listen(listen_socket, SOMAXCONN);
  if (listen_res < 0) {
    if (error_message != nullptr) {
      *error_message = "Could not listen socket: ";
      error_message->append(strerror(errno));
    }
    close_socket_impl(listen_socket);
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

bool listener::serve(const std::string& ip_address, uint16_t port, handler new_connection_handler, std::string* error_message) {
  auto socket = open_socket_impl(ip_address, port, error_message, false);

  if (socket == invalid_socket_id) {
    return false;
  }

  while (!_is_terminating.load(std::memory_order::relaxed)) {
    sockaddr_storage in_addr_storage;  // NOLINT(*member-init*)
    socklen_t in_len = sizeof(in_addr_storage);

    const auto accept_sock = ::accept(socket, reinterpret_cast<sockaddr*>(&in_addr_storage), &in_len);  // NOLINT(*-reinterpret-cast)
    if (accept_sock == invalid_socket_id) {
      continue;
    }

    // set non blocking mode
    if (!set_non_blocking_mode(accept_sock, nullptr)) {
      close_socket_impl(accept_sock);
      continue;
    }

    std::array<char, NI_MAXHOST> host_name_buf;  // NOLINT(*member-init*)

    const auto name_info_res = ::getnameinfo(reinterpret_cast<sockaddr*>(&in_addr_storage), sizeof(in_addr_storage), host_name_buf.data(), host_name_buf.size(), nullptr, 0, 0);  // NOLINT(*-reinterpret-cast)

    std::string_view host_name;
    if (name_info_res != 0) {
      host_name_buf[0] = '\0';
    } else {
      host_name = {host_name_buf.data()};
    }
    // TODO:: add to epoll

    new_connection_handler(connection_id{accept_sock}, host_name);
  }

  close_socket_impl(socket);

  return true;
}

void listener::shut_down() noexcept {
  _is_terminating.store(true, std::memory_order::release);
}

}  // namespace server::socket_layer
