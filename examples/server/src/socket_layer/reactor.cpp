#include <async_coro/config.h>
#include <async_coro/thread_safety/unique_lock.h>
#include <server/socket_layer/connection_id.h>
#include <server/socket_layer/reactor.h>
#include <server/utils/expected.h>

#include <chrono>
#include <iostream>
#include <span>

#if EPOLL_SOCKET && !WIN_SOCKET
#include <sys/epoll.h>
#endif
namespace server::socket_layer {

static void epoll_ctl_impl(epoll_handle_t epoll_fd, socket_type socked_descriptor, int action, uint32_t flags) {  // NOLINT(*swappable*)
#if EPOLL_SOCKET

  epoll_event event{};
  event.data.fd = socked_descriptor;
  event.events = flags;
  if (-1 == ::epoll_ctl(epoll_fd, action, socked_descriptor, &event) && errno != EEXIST) {
    std::cerr << "epoll_ctl error: " << strerror(errno) << '\n';
  }

#elif KQUEUE_SOCKET

  struct kevent ev_set;
  EV_SET(&ev_set, socked_descriptor, flags, action, 0, 0, nullptr);
  if (-1 == ::kevent(epoll_fd, &ev_set, 1, nullptr, 0, nullptr)) {
    std::cerr << "kevent set error: " << strerror(errno) << '\n';
  }

#endif
};

enum class reactor_data_await_type : uint8_t {
  send_data,
  receive_data
};

struct reactor::await_callback {
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

  std::span events_to_process{events.data(), static_cast<size_t>(n_events)};
  for (const auto& event : events_to_process) {
#if EPOLL_SOCKET
    const auto event_flags = event.events;
    const auto event_fd = event.data.fd;
#elif KQUEUE_SOCKET
    const auto event_flags = event.flags;
    const auto event_fd = event.ident;
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

    continue_callback_t continuation;
    reactor_data_await_type awaited_for = reactor_data_await_type::receive_data;
    {
      async_coro::unique_lock lock{_mutex};
      auto cont_it = std::ranges::find_if(_continuations, [event_fd](const auto& pair) { return pair.id.get_platform_id() == event_fd; });
      if (cont_it != _continuations.end()) {
        awaited_for = cont_it->await;
        if (!is_error) {
          // skip unwanted events
          if (awaited_for == reactor_data_await_type::send_data && !is_write_available) {
            continue;
          }
          if (awaited_for == reactor_data_await_type::receive_data && !is_read_available) {
            continue;
          }
        }

        continuation = std::move(cont_it->callback);
        if (_continuations.size() > 1) {
          // move last element to current
          *cont_it = std::move(_continuations.back());
        }
        _continuations.pop_back();
      } else {
        ASYNC_CORO_ASSERT(cont_it != _continuations.end());
      }
    }

    if (!continuation) {
      if (is_error) {
        close_connection(connection_id{event_fd});
      }
      continue;
    }

    if (is_error) {
      // Handle errors on socket associated with event_fd.
      continuation(connection_state::closed);
    } else {
      // Data available on existing sockets. Wake up the coroutine associated with event_fd.
      if (awaited_for == reactor_data_await_type::receive_data) {
        continuation(connection_state::available_read);
      } else {
        continuation(connection_state::available_write);
      }
    }
  }
}

void reactor::add_connection(connection_id conn) {  // NOLINT(*-const)
#if WIN_SOCKET
  epoll_ctl_impl(_epoll_fd, conn.get_platform_id(), EPOLL_CTL_ADD, EPOLLIN | EPOLLOUT | EPOLLRDHUP);
#elif EPOLL_SOCKET
  epoll_ctl_impl(_epoll_fd, conn.get_platform_id(), EPOLL_CTL_ADD, EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLET);
#elif KQUEUE_SOCKET
  epoll_ctl_impl(_epoll_fd, conn.get_platform_id(), EV_ADD, EVFILT_READ | EVFILT_WRITE);
#endif
}

void reactor::close_connection(connection_id conn) {  // NOLINT(*-const)
#if EPOLL_SOCKET
  epoll_ctl_impl(_epoll_fd, conn.get_platform_id(), EPOLL_CTL_DEL, 0);
#elif KQUEUE_SOCKET
  epoll_ctl_impl(_epoll_fd, conn.get_platform_id(), EV_DELETE, 0);
#endif

  close_socket(conn.get_platform_id());
}

void reactor::continue_after_receive_data(connection_id conn, continue_callback_t&& clb) {
  ASYNC_CORO_ASSERT(std::ranges::find_if(_continuations, [conn](const auto& pair) { return pair.id == conn; }) == _continuations.end());

  async_coro::unique_lock lock{_mutex};
  _continuations.emplace_back(std::move(clb), conn, reactor_data_await_type::receive_data);
}

void reactor::continue_after_sent_data(connection_id conn, continue_callback_t&& clb) {
  ASYNC_CORO_ASSERT(std::ranges::find_if(_continuations, [conn](const auto& pair) { return pair.id == conn; }) == _continuations.end());

  async_coro::unique_lock lock{_mutex};
  _continuations.emplace_back(std::move(clb), conn, reactor_data_await_type::send_data);
}

}  // namespace server::socket_layer
