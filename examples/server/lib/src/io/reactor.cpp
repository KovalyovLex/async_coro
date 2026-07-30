#include <async_coro/config.h>
#include <async_coro/thread_safety/unique_lock.h>
#include <server/io/io_config.h>
#include <server/io/reactor.h>
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
#else
#error "Unsupported platform"
#endif

#if !WIN_SOCKET
#include <unistd.h>
#endif

namespace server::io {

static void epoll_ctl_impl(epoll_handle_t event_fd, socket_type file_descriptor, int action, uint32_t flags, void* user_data) {  // NOLINT(bugprone-easily-swappable-parameters)
#if EPOLL_SOCKET
  epoll_event event{};
  event.data.ptr = user_data;
  event.events = flags;
  if (-1 == ::epoll_ctl(event_fd, action, file_descriptor, &event)) {
    std::cerr << "epoll_ctl error: " << strerror(errno) << '\n';
  }

#elif KQUEUE_SOCKET
  struct kevent ev_set;
  EV_SET(&ev_set, file_descriptor, flags, action, 0, 0, user_data);
  if (-1 == ::kevent(event_fd, &ev_set, 1, nullptr, 0, nullptr)) {
    std::cerr << "kevent set error: " << strerror(errno) << '\n';
  }

#endif
}

reactor::reactor() noexcept {
#if EPOLL_SOCKET
  _epoll_fd = epoll_create1(0);
#elif KQUEUE_SOCKET
  _epoll_fd = kqueue();
#endif
}

reactor::~reactor() noexcept {
  close_epoll(_epoll_fd);
}

void reactor::process_loop(std::chrono::nanoseconds max_wait) {
  constexpr int MAXEVENTS = 64;

  struct continuation {
    continue_callback_t continuation;
    reactor::await_type awaited_for = reactor::await_type::receive_data;
    bool is_error = false;
  };

  std::array<continuation, MAXEVENTS> continuation_data;
  size_t num_continuations = 0;

#if EPOLL_SOCKET
  std::array<epoll_event, MAXEVENTS> events{};

  auto timeout_ms = std::chrono::duration_cast<std::chrono::milliseconds>(max_wait).count();
  int n_events = ::epoll_wait(_epoll_fd, events.data(), events.size(), static_cast<int>(timeout_ms));

  if (n_events == -1) {
    std::cerr << "epoll_wait error: " << strerror(errno) << '\n';
  }

  if (n_events <= 0) {
    return;
  }

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

      auto& continue_struct = continuation_data[num_continuations];  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index): dynamic index from event count is bounded by MAXEVENTS

      continue_struct.is_error = is_error;
      {
        auto& fd_info = _handled_fds[index];

        continue_struct.awaited_for = fd_info.await;
        if (!is_error) {
          // skip unwanted events
          if (continue_struct.awaited_for == reactor::await_type::send_data && !is_write_available) {
            continue;
          }
          if (continue_struct.awaited_for == reactor::await_type::receive_data && !is_read_available) {
            continue;
          }
        }

        continue_struct.continuation = std::move(fd_info.callback);
        fd_info.await = reactor::await_type::no_await;
      }

      if (!continue_struct.continuation) {
        continue;
      }
      num_continuations++;
    }
  }

#elif KQUEUE_SOCKET
  std::array<struct kevent, MAXEVENTS> events{};

  timespec timeout{};
  timeout.tv_sec = std::chrono::duration_cast<std::chrono::seconds>(max_wait).count();
  timeout.tv_nsec = max_wait.count();

  int n_events = ::kevent(_epoll_fd, nullptr, 0, events.data(), events.size(), &timeout);

  if (n_events == -1) {
    std::cerr << "kevent error: " << strerror(errno) << '\n';
  }

  if (n_events <= 0) {
    return;
  }

  {
    std::span events_to_process{events.data(), static_cast<size_t>(n_events)};

    async_coro::unique_lock lock{_mutex};
    for (const auto& event : events_to_process) {
      const auto event_flags = event.flags;
      void* user_data = event.udata;

      const bool is_error = (event_flags & EV_ERROR) != 0;
      const bool is_read_available = !is_error;
      const bool is_write_available = !is_error;

      auto index = static_cast<size_t>(reinterpret_cast<uintptr_t>(user_data));  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast): converting user-data pointer back to index

      auto& continue_struct = continuation_data[num_continuations];  // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index): dynamic index from event count is bounded by MAXEVENTS

      continue_struct.is_error = is_error;
      {
        auto& fd_info = _handled_fds[index];

        continue_struct.awaited_for = fd_info.await;
        if (!is_error) {
          // skip unwanted events
          if (continue_struct.awaited_for == reactor::await_type::send_data && !is_write_available) {
            continue;
          }
          if (continue_struct.awaited_for == reactor::await_type::receive_data && !is_read_available) {
            continue;
          }
        }

        continue_struct.continuation = std::move(fd_info.callback);
        fd_info.await = reactor::await_type::no_await;
      }

      if (!continue_struct.continuation) {
        continue;
      }
      num_continuations++;
    }
  }

#endif

  std::span continuations_to_process{continuation_data.data(), num_continuations};
  for (auto& continue_struct : continuations_to_process) {
    if (continue_struct.is_error) {
      // Handle errors on fd associated with event_fd.
      continue_struct.continuation(connection_state::closed);
    } else {
      // Data available on existing fds. Wake up the coroutine associated with event_fd.
      if (continue_struct.awaited_for == reactor::await_type::receive_data) {
        continue_struct.continuation(connection_state::available_read);
      } else {
        continue_struct.continuation(connection_state::available_write);
      }
    }
    continue_struct.continuation = nullptr;
  }
}

size_t reactor::add_sock(socket_type sock) {
  size_t index = 0;
  {
    async_coro::unique_lock lock{_mutex};

    if (!_empty_fds.empty()) {
      index = _empty_fds.back();
      _empty_fds.pop_back();
      auto& fd_info = _handled_fds[index];
      fd_info.sock = sock;
      fd_info.await = reactor::await_type::no_await;
    } else {
      index = _handled_fds.size();
      _handled_fds.emplace_back(continue_callback_t{}, sock, reactor::await_type::no_await);
    }
  }

#if WIN_SOCKET
  epoll_ctl_impl(_epoll_fd, sock, EPOLL_CTL_ADD, EPOLLIN | EPOLLOUT | EPOLLRDHUP, reinterpret_cast<void*>(static_cast<uintptr_t>(index)));
#elif EPOLL_SOCKET
  epoll_ctl_impl(_epoll_fd, sock, EPOLL_CTL_ADD, EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLET, reinterpret_cast<void*>(static_cast<uintptr_t>(index)));
#elif KQUEUE_SOCKET
  epoll_ctl_impl(_epoll_fd, sock, EV_ADD, EVFILT_READ | EVFILT_WRITE, reinterpret_cast<void*>(static_cast<uintptr_t>(index)));
#endif

  return index;
}

void reactor::remove_sock(socket_type sock, size_t index) {
  {
    async_coro::unique_lock lock{_mutex};

    ASYNC_CORO_ASSERT(index < _handled_fds.size());

    auto& fd_info = _handled_fds[index];
    fd_info.callback = {};
    fd_info.sock = invalid_socket_id;
    fd_info.await = reactor::await_type::no_await;

    _empty_fds.push_back(index);
  }

#if EPOLL_SOCKET
  epoll_ctl_impl(_epoll_fd, sock, EPOLL_CTL_DEL, 0, nullptr);
#elif KQUEUE_SOCKET
  epoll_ctl_impl(_epoll_fd, sock, EV_DELETE, 0, nullptr);
#endif

  close_socket(sock);
}

void reactor::continue_after_receive_data(socket_type sock, size_t index, continue_callback_t&& callback) {  // NOLINT(bugprone-easily-swappable-parameters)
  async_coro::unique_lock lock{_mutex};

  ASYNC_CORO_ASSERT(index < _handled_fds.size());

  auto& fd_info = _handled_fds[index];
  ASYNC_CORO_ASSERT(fd_info.sock == sock);

  fd_info.callback = std::move(callback);
  fd_info.await = reactor::await_type::receive_data;
}

void reactor::continue_after_sent_data(socket_type sock, size_t index, continue_callback_t&& callback) {  // NOLINT(bugprone-easily-swappable-parameters)
  async_coro::unique_lock lock{_mutex};

  ASYNC_CORO_ASSERT(index < _handled_fds.size());

  auto& fd_info = _handled_fds[index];
  ASYNC_CORO_ASSERT(fd_info.sock == sock);

  fd_info.callback = std::move(callback);
  fd_info.await = reactor::await_type::send_data;
}

}  // namespace server::io
