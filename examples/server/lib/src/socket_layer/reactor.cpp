#include <async_coro/config.h>
#include <async_coro/thread_safety/unique_lock.h>
#include <server/socket_layer/connection_id.h>
#include <server/socket_layer/reactor.h>
#include <server/utils/expected.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <span>

#if EPOLL_SOCKET
#if WIN_SOCKET
#include <wepoll.h>
#else
#include <sys/epoll.h>
#endif  // WIN_SOCKET
#elif KQUEUE_SOCKET
#include <sys/event.h>
#endif

namespace server::socket_layer {

static void epoll_ctl_impl(epoll_handle_t epoll_fd, socket_type socked_descriptor, int action, uint32_t flags, void* user_data) {  // NOLINT(*swappable*)
#if EPOLL_SOCKET

  epoll_event event{};
  event.data.ptr = user_data;
  event.events = flags;
  if (-1 == ::epoll_ctl(epoll_fd, action, socked_descriptor, &event)) {
    std::cerr << "epoll_ctl error: " << strerror(errno) << '\n';
  }

#elif KQUEUE_SOCKET

  struct kevent ev_set;
  EV_SET(&ev_set, socked_descriptor, flags, action, 0, 0, user_data);
  if (-1 == ::kevent(epoll_fd, &ev_set, 1, nullptr, 0, nullptr)) {
    std::cerr << "kevent set error: " << strerror(errno) << '\n';
  }

#endif
};

enum class reactor_data_await_type : uint8_t {
  send_data,
  receive_data,
  no_await,
};

struct reactor::handled_connection {
  continue_callback_t callback;
  connection_id id;
  reactor_data_await_type await;
};

reactor::~reactor() noexcept = default;

reactor::reactor() noexcept {
#if EPOLL_SOCKET
  _epoll_fd = epoll_create1(0);  // NOLINT(*initializer*)

#elif KQUEUE_SOCKET
  _epoll_fd = kqueue();

#endif
}

void reactor::process_loop(std::chrono::nanoseconds max_wait) {  // NOLINT(*-complexity)
  constexpr int MAXEVENTS = 64;

#if EPOLL_SOCKET
  std::array<epoll_event, MAXEVENTS> events;  // NOLINT(*-member-init*)

#elif KQUEUE_SOCKET
  std::array<struct kevent, MAXEVENTS> events{};

  timespec timeout{};
  timeout.tv_sec = std::chrono::duration_cast<std::chrono::seconds>(max_wait).count();
  timeout.tv_nsec = max_wait.count();
#endif

#if EPOLL_SOCKET
  int n_events = ::epoll_wait(_epoll_fd, events.data(), events.size(), std::chrono::duration_cast<std::chrono::milliseconds>(max_wait).count());  // NOLINT(*narrow*)

  if (n_events == -1) {
    std::cerr << "epoll_wait error: " << strerror(errno) << '\n';
  }
#elif KQUEUE_SOCKET
  int n_events = ::kevent(_epoll_fd, nullptr, 0, events.data(), events.size(), &timeout);

  if (n_events == -1) {
    std::cerr << "kevent error: " << strerror(errno) << '\n';
  }
#endif

  if (n_events <= 0) {
    return;
  }

  struct continuation {
    continue_callback_t continuation;
    reactor_data_await_type awaited_for = reactor_data_await_type::receive_data;
    bool is_error = false;
  };

  std::array<continuation, MAXEVENTS> continuation_data;  // NOLINT(*-member-init*)
  size_t num_continuations = 0;

  {
    std::span events_to_process{events.data(), static_cast<size_t>(n_events)};

    async_coro::unique_lock lock{_mutex};
    for (const auto& event : events_to_process) {
      void* user_data = nullptr;

#if EPOLL_SOCKET
      const auto event_flags = event.events;
      user_data = event.data.ptr;
#elif KQUEUE_SOCKET
      const auto event_flags = event.flags;
      user_data = event.udata;
#endif

#if EPOLL_SOCKET
      const bool is_error = (event_flags & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) != 0;
      const bool is_read_available = (event_flags & EPOLLIN) == EPOLLIN;
      const bool is_write_available = (event_flags & EPOLLOUT) == EPOLLOUT;
#elif KQUEUE_SOCKET
      const bool is_error = (event_flags & EV_ERROR) != 0;
      const bool is_read_available = !is_error;
      const bool is_write_available = !is_error;
#endif

      auto index = reinterpret_cast<uintptr_t>(user_data);  // NOLINT(*reinterpret-cast*)

      auto& continue_struct = continuation_data[num_continuations];  // NOLINT(*array-index)

      continue_struct.is_error = is_error;
      {
        auto& connection = _handled_connections[index];

        continue_struct.awaited_for = connection.await;
        if (!is_error) {
          // skip unwanted events
          if (continue_struct.awaited_for == reactor_data_await_type::send_data && !is_write_available) {
            continue;
          }
          if (continue_struct.awaited_for == reactor_data_await_type::receive_data && !is_read_available) {
            continue;
          }
        }

        continue_struct.continuation = std::move(connection.callback);
        connection.await = reactor_data_await_type::no_await;
      }

      if (!continue_struct.continuation) {
        continue;
      }
      num_continuations++;
    }
  }

  std::span continuations_to_process{continuation_data.data(), num_continuations};
  for (auto& continue_struct : continuations_to_process) {
    if (continue_struct.is_error) {
      // Handle errors on socket associated with event_fd.
      continue_struct.continuation(connection_state::closed);
    } else {
      // Data available on existing sockets. Wake up the coroutine associated with event_fd.
      if (continue_struct.awaited_for == reactor_data_await_type::receive_data) {
        continue_struct.continuation(connection_state::available_read);
      } else {
        continue_struct.continuation(connection_state::available_write);
      }
    }
    continue_struct.continuation = nullptr;
  }
}

size_t reactor::add_connection(connection_id conn) {
  uintptr_t index = 0;
  {
    async_coro::unique_lock lock{_mutex};

    if (!_empty_connections.empty()) {
      index = _empty_connections.back();
      _empty_connections.pop_back();
      auto& connection = _handled_connections[index];
      connection.id = conn;
      connection.await = reactor_data_await_type::no_await;
    } else {
      index = _handled_connections.size();
      _handled_connections.emplace_back(continue_callback_t{}, conn, reactor_data_await_type::no_await);
    }
  }

  auto* user_data = reinterpret_cast<void*>(index);  // NOLINT(*reinterpret-cast*, *int-to-ptr*)

#if WIN_SOCKET
  epoll_ctl_impl(_epoll_fd, conn.get_platform_id(), EPOLL_CTL_ADD, EPOLLIN | EPOLLOUT | EPOLLRDHUP, user_data);
#elif EPOLL_SOCKET
  epoll_ctl_impl(_epoll_fd, conn.get_platform_id(), EPOLL_CTL_ADD, EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLET, user_data);
#elif KQUEUE_SOCKET
  epoll_ctl_impl(_epoll_fd, conn.get_platform_id(), EV_ADD, EVFILT_READ | EVFILT_WRITE, user_data);
#endif

  return index;
}

void reactor::close_connection(connection_id conn, size_t index) {
  {
    async_coro::unique_lock lock{_mutex};

    ASYNC_CORO_ASSERT(index < _handled_connections.size());

    auto& connection = _handled_connections[index];
    connection.callback = {};
    connection.id = invalid_connection;
    connection.await = reactor_data_await_type::no_await;

    _empty_connections.push_back(index);
  }

#if EPOLL_SOCKET
  epoll_ctl_impl(_epoll_fd, conn.get_platform_id(), EPOLL_CTL_DEL, 0, nullptr);
#elif KQUEUE_SOCKET
  epoll_ctl_impl(_epoll_fd, conn.get_platform_id(), EV_DELETE, 0, nullptr);
#endif

  close_socket(conn.get_platform_id());
}

void reactor::continue_after_receive_data(ASYNC_CORO_ASSERT_VARIABLE connection_id conn, size_t index, continue_callback_t&& clb) {
  async_coro::unique_lock lock{_mutex};

  ASYNC_CORO_ASSERT(index < _handled_connections.size());

  auto& connection = _handled_connections[index];
  ASYNC_CORO_ASSERT(connection.id == conn);

  connection.callback = std::move(clb);
  connection.await = reactor_data_await_type::receive_data;
}

void reactor::continue_after_sent_data(connection_id conn, size_t index, continue_callback_t&& clb) {
  async_coro::unique_lock lock{_mutex};

  ASYNC_CORO_ASSERT(index < _handled_connections.size());

  auto& connection = _handled_connections[index];
  ASYNC_CORO_ASSERT(connection.id == conn);

  connection.callback = std::move(clb);
  connection.await = reactor_data_await_type::send_data;
}

}  // namespace server::socket_layer
