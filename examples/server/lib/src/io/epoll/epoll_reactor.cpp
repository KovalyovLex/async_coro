#include <server/io/io_config.h>

#if EPOLL_SOCKET

#include <async_coro/config.h>
#include <async_coro/utils/always_false.h>
#include <server/core/error.h>
#include <server/io/epoll/epoll_reactor.h>
#include <server/io/epoll/epoll_socket.h>
#include <server/utils/expected.h>

// Linux socket headers
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <type_traits>
#include <variant>

namespace server::io {

// NOLINTBEGIN(*-member-initializer)
epoll_reactor::epoll_reactor() noexcept {
  _epoll_fd = epoll_create1(0);
}
// NOLINTEND(*-member-initializer)

epoll_reactor::~epoll_reactor() noexcept {
  io::close_file(_epoll_fd);
}

void epoll_reactor::process_loop(std::chrono::nanoseconds max_wait) {
  constexpr int MAXEVENTS = 64;
  std::array<epoll_event, MAXEVENTS> events{};

  auto timeout_ms = std::chrono::duration_cast<std::chrono::milliseconds>(max_wait).count();
  int n_events = ::epoll_wait(_epoll_fd, events.data(), events.size(), static_cast<int>(timeout_ms));

  if (n_events == -1) {
    std::cerr << "epoll_wait error: " << strerror(errno) << '\n';
    return;
  }

  if (n_events <= 0) {
    return;
  }

  struct continuation {
    request_variant op;
    bool is_error = false;
  };

  std::array<continuation, MAXEVENTS> continuation_data;
  size_t num_continuations = 0;

  {
    std::span events_to_process{events.data(), static_cast<size_t>(n_events)};

    async_coro::unique_lock lock{_mutex};
    for (const auto& event : events_to_process) {
      const auto event_flags = event.events;
      void* user_data = event.data.ptr;

      const bool is_error = (event_flags & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) != 0;
      const bool is_read_available = (event_flags & EPOLLIN) == EPOLLIN;
      const bool is_write_available = (event_flags & EPOLLOUT) == EPOLLOUT;

      auto index = static_cast<size_t>(reinterpret_cast<uintptr_t>(user_data));  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast): converting user-data pointer back to index

      if (index >= _handled_fds.size()) {
        continue;
      }

      auto& cont = continuation_data[num_continuations];  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index): dynamic index from event count is bounded by MAXEVENTS
      cont.is_error = is_error;

      auto& fd_info = _handled_fds[index];

      if (!is_error) {
        // skip unwanted events
        if (std::holds_alternative<op_send_socket>(fd_info.pending_op)) {
          if (!is_write_available) {
            continue;
          }
        }
        if (std::holds_alternative<op_receive_socket>(fd_info.pending_op)) {
          if (!is_read_available) {
            continue;
          }
        }
      }

      // Move the pending operation out of the guarded vector
      cont.op = std::move(fd_info.pending_op);
      fd_info.pending_op = no_op{};

      if (std::holds_alternative<no_op>(cont.op)) {
        continue;
      }
      num_continuations++;
    }
  }

  std::span continuations_to_process{continuation_data.data(), num_continuations};
  for (auto& cont : continuations_to_process) {
    if (cont.is_error) {
      // Handle errors on fd associated with event_fd.
      std::visit([](auto& operation) {
        using op_type = std::decay_t<decltype(operation)>;

        if (!operation.callback) {
          return;
        }

        if constexpr (std::is_same_v<op_type, op_accept_socket>) {
          operation.callback(expected<socket_type, core::error>{unexpect, core::error{core::error_type::accept_failed, ECONNABORTED}});
        } else if constexpr (std::is_same_v<op_type, op_connect_socket>) {
          operation.callback(expected<void, core::error>{unexpect, core::error{core::error_type::connect_failed, ECONNABORTED}});
        } else if constexpr (std::is_same_v<op_type, op_send_socket>) {
          operation.callback(expected<size_t, core::error>{unexpect, core::error{core::error_type::write_failed, ECONNABORTED}});
        } else if constexpr (std::is_same_v<op_type, op_receive_socket>) {
          operation.callback(expected<size_t, core::error>{unexpect, core::error{core::error_type::read_failed, ECONNABORTED}});
        }
      },
                 cont.op);
    } else {
      // Data available on existing fds. Perform the actual I/O operation.
      std::visit([](auto& operation) {
        using op_type = std::decay_t<decltype(operation)>;

        if (!operation.callback) {
          return;
        }

        if constexpr (std::is_same_v<op_type, op_send_socket>) {
          operation.callback(expected<size_t, core::error>{});
        } else if constexpr (std::is_same_v<op_type, op_receive_socket>) {
          operation.callback(expected<size_t, core::error>{});
        } else if constexpr (std::is_same_v<op_type, op_accept_socket>) {
          // Accept will be handled by submit_accept_socket which does the actual accept() call
          // in the callback. Here we just signal that the listen fd is ready.
          operation.callback(expected<socket_type, core::error>{});
        } else if constexpr (std::is_same_v<op_type, op_connect_socket>) {
          operation.callback(expected<void, core::error>{});
        }
      },
                 cont.op);
    }
  }
}

expected<socket_type, core::error> epoll_reactor::create_socket(socket_type_id kind) noexcept {
  const int domain = AF_INET;
  const int type = (kind == socket_type_id::tcp) ? SOCK_STREAM : SOCK_DGRAM;
  const int protocol = 0;

  const socket_type sock = socket(domain, type, protocol);
  if (sock < 0) {
    return expected<socket_type, core::error>{unexpect, core::error{core::error_type::create_socket_failed, errno}};
  }

  // Set non-blocking mode
  const int flags = fcntl(sock, F_GETFL, 0);
  if (flags < 0) {
    auto err = expected<socket_type, core::error>{unexpect, core::error{core::error_type::create_socket_failed, errno}};

    close_socket(sock);
    return err;
  }

  if (fcntl(sock, F_SETFL, flags | O_NONBLOCK) < 0) {
    auto err = expected<socket_type, core::error>{unexpect, core::error{core::error_type::create_socket_failed, errno}};

    close_socket(sock);
    return err;
  }

  return sock;
}

expected<void, core::error> epoll_reactor::bind_socket(socket_type socket_handle, std::span<const std::byte> address) noexcept {
  const int result = ::bind(socket_handle,
                            reinterpret_cast<const struct sockaddr*>(address.data()),  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast): required by POSIX bind() API
                            static_cast<socklen_t>(address.size() / sizeof(std::byte)));
  if (result < 0) {
    return expected<void, core::error>{unexpect, core::error{core::error_type::bind_failed, errno}};
  }
  return expected<void, core::error>{};
}

expected<void, core::error> epoll_reactor::listen_socket(socket_type socket_handle, int backlog) noexcept {
  const int result = listen(socket_handle, backlog);
  if (result < 0) {
    return expected<void, core::error>{unexpect, core::error{core::error_type::listen_failed, errno}};
  }
  return expected<void, core::error>{};
}

expected<size_t, core::error> epoll_reactor::add_sock(socket_type sock) {
  size_t index = 0;
  {
    async_coro::unique_lock lock{_mutex};

    if (!_empty_fds.empty()) {
      index = _empty_fds.back();
      _empty_fds.pop_back();
      auto& fd_info = _handled_fds[index];
      fd_info.sock = sock;
      fd_info.pending_op = no_op{};
    } else {
      index = _handled_fds.size();
      _handled_fds.emplace_back(fd_state{});
      auto& fd_info = _handled_fds[index];
      fd_info.sock = sock;
      fd_info.pending_op = no_op{};
    }
  }

  auto res = epoll_ctl_impl(sock, EPOLL_CTL_ADD, EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLET, index);
  if (!res) {
    return expected<size_t, core::error>{unexpect, res.error()};
  }

  return index;
}

expected<void, core::error> epoll_reactor::remove_sock(socket_type sock, size_t index) {  // NOLINT(*swappable*)
  {
    async_coro::unique_lock lock{_mutex};

    ASYNC_CORO_ASSERT(index < _handled_fds.size());

    auto& fd_info = _handled_fds[index];
    fd_info.pending_op = no_op{};
    fd_info.sock = invalid_socket_id;

    _empty_fds.push_back(index);
  }

  return epoll_ctl_impl(sock, EPOLL_CTL_DEL, 0, 0);
}

void epoll_reactor::submit_send_socket(epoll_socket& socket, continue_size_callback_t&& callback) {
  socket.check_subscribed();
  ASYNC_CORO_ASSERT(!socket.is_closed());

  async_coro::unique_lock lock{_mutex};
  ASYNC_CORO_ASSERT(socket._index < _handled_fds.size());

  auto res = epoll_ctl_impl(socket.get_native_handle(), EPOLL_CTL_MOD, EPOLLOUT | EPOLLRDHUP | EPOLLET, socket._index);
  if (!res) {
    if (callback) {
      callback(expected<size_t, core::error>{unexpect, res.error()});
    }
    return;
  }

  auto& fd_info = _handled_fds[socket._index];
  ASYNC_CORO_ASSERT(std::holds_alternative<no_op>(fd_info.pending_op));

  fd_info.pending_op = op_send_socket{std::move(callback)};
}

void epoll_reactor::submit_receive_socket(epoll_socket& socket, continue_size_callback_t&& callback) {
  ASYNC_CORO_ASSERT(!socket.is_closed());

  socket.check_subscribed();

  async_coro::unique_lock lock{_mutex};
  ASYNC_CORO_ASSERT(socket._index < _handled_fds.size());

  auto res = epoll_ctl_impl(socket.get_native_handle(), EPOLL_CTL_MOD, EPOLLIN | EPOLLRDHUP | EPOLLET, socket._index);
  if (!res) {
    if (callback) {
      callback(expected<size_t, core::error>{unexpect, res.error()});
    }
    return;
  }

  auto& fd_info = _handled_fds[socket._index];
  ASYNC_CORO_ASSERT(std::holds_alternative<no_op>(fd_info.pending_op));

  fd_info.pending_op = op_receive_socket{std::move(callback)};
}

void epoll_reactor::submit_accept_socket(epoll_socket& socket, continue_socket_callback_t&& callback) {
  ASYNC_CORO_ASSERT(!socket.is_closed());

  socket.check_subscribed();

  async_coro::unique_lock lock{_mutex};
  ASYNC_CORO_ASSERT(socket._index < _handled_fds.size());

  auto res = epoll_ctl_impl(socket.get_native_handle(), EPOLL_CTL_MOD, EPOLLIN | EPOLLRDHUP | EPOLLET, socket._index);
  if (!res) {
    if (callback) {
      callback(expected<socket_type, core::error>{unexpect, res.error()});
    }
    return;
  }

  auto& fd_info = _handled_fds[socket._index];
  ASYNC_CORO_ASSERT(std::holds_alternative<no_op>(fd_info.pending_op));

  fd_info.pending_op = op_accept_socket{std::move(callback)};
}

void epoll_reactor::submit_connect_socket(epoll_socket& socket, continue_void_callback_t&& callback) {
  socket.check_subscribed();
  ASYNC_CORO_ASSERT(!socket.is_closed());

  // Re-arm epoll for write events (connect completion is signaled by writability)
  auto res = epoll_ctl_impl(socket.get_native_handle(), EPOLL_CTL_MOD, EPOLLOUT | EPOLLRDHUP | EPOLLET, socket._index);

  if (!res) {
    if (callback) {
      callback(res);
    }
    return;
  }

  async_coro::unique_lock lock{_mutex};
  ASYNC_CORO_ASSERT(socket._index < _handled_fds.size());

  auto& fd_info = _handled_fds[socket._index];
  ASYNC_CORO_ASSERT(fd_info.sock == socket._sock);
  ASYNC_CORO_ASSERT(std::holds_alternative<no_op>(fd_info.pending_op));

  fd_info.pending_op = op_connect_socket{std::move(callback)};
}

void epoll_reactor::submit_close_socket(socket_type socket_handle, continue_void_callback_t&& callback) {
  // For epoll, we can close immediately since there's no async close operation.
  // We still invoke the callback to maintain API consistency.
  if (socket_handle != invalid_socket_id) {
    close_socket(socket_handle);
  }
  if (callback) {
    callback(expected<void, core::error>{});
  }
}

expected<void, core::error> epoll_reactor::epoll_ctl_impl(socket_type socket_handle, int action, uint32_t flags, size_t index) const {
  epoll_event event{};
  event.data.ptr = reinterpret_cast<void*>(index);  // NOLINT(*-reinterpret-cast, *-int-to-ptr)
  event.events = flags;
  if (-1 == ::epoll_ctl(_epoll_fd, action, socket_handle, &event)) {
    return expected<void, core::error>{unexpect, core::error{core::error_type::epoll_ctl_failed, errno}};
  }
  return {};
}

}  // namespace server::io

#endif  // EPOLL_SOCKET
