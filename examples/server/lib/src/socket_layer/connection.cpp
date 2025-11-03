
#include <async_coro/await/await_callback.h>
#include <server/socket_layer/connection.h>
#include <server/socket_layer/connection_id.h>
#include <server/socket_layer/reactor.h>
#include <server/socket_layer/ssl_connection.h>
#include <server/socket_layer/ssl_context.h>
#include <server/utils/expected.h>

#if !WIN_SOCKET
#include <sys/socket.h>
#endif

#include <cerrno>

namespace server::socket_layer {

connection::~connection() noexcept {
  if (_reactor != nullptr) {
    close_connection();
  }
}

void connection::close_connection() {
  if (_subscription_index != k_invalid_index) {
    _reactor->close_connection(_sock, _subscription_index);
    _subscription_index = k_invalid_index;
  } else if (_sock != invalid_connection) {
    close_socket(_sock.get_platform_id());
  }

  _reactor = nullptr;
}

async_coro::task<expected<void, std::string>> connection::write_buffer(std::span<const std::byte> bytes) {  // NOLINT(*-complexity*)
  using res_t = expected<void, std::string>;

  if (_reactor == nullptr) [[unlikely]] {
    co_return res_t{unexpect, "Connection was already closed"};
  }

  if (_ssl) {
    while (true) {
      const auto sent_local = _ssl.write(bytes);

      if (sent_local > 0) {
        if (sent_local == bytes.size()) {
          co_return res_t{};
        } else {
          bytes = bytes.subspan(sent_local);
        }

        check_subscribed();

        auto res = co_await async_coro::await_callback_with_result<reactor::connection_state>([this](auto cont) {
          _reactor->continue_after_sent_data(_sock, _subscription_index, std::move(cont));
        });
        if (res == reactor::connection_state::closed) {
          close_connection();
          co_return res_t{unexpect, "Connection was closed"};
        }
        continue;
      }

      const auto error = _ssl.get_error(sent_local);

      if (error == ssl_error::connection_was_closed) {
        close_connection();
        co_return res_t{};
      }

      if (error == ssl_error::wants_read && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        check_subscribed();

        auto res = co_await async_coro::await_callback_with_result<reactor::connection_state>([this](auto cont) {
          _reactor->continue_after_receive_data(_sock, _subscription_index, std::move(cont));
        });
        if (res == reactor::connection_state::closed) {
          close_connection();
          co_return res_t{};
        }
      } else if (error == ssl_error::wants_write && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        check_subscribed();

        auto res = co_await async_coro::await_callback_with_result<reactor::connection_state>([this](auto cont) {
          _reactor->continue_after_sent_data(_sock, _subscription_index, std::move(cont));
        });
        if (res == reactor::connection_state::closed) {
          close_connection();
          co_return res_t{};
        }
      } else {
        co_return res_t{unexpect, ssl_context::get_ssl_error()};
      }
    }

    co_return res_t{};
  }

  const auto fd_id = _sock.get_platform_id();

  while (true) {
    const auto sent_local = ::send(fd_id, bytes.data(), bytes.size(), 0);
    bool is_error = true;

    if (sent_local > 0) {
      is_error = false;

      if (sent_local == bytes.size()) {
        co_return res_t{};
      } else {
        bytes = bytes.subspan(sent_local);
      }
    } else if (sent_local == 0 || errno == EAGAIN || errno == EWOULDBLOCK) {
      is_error = false;
    }

    if (is_error) {
      co_return res_t{unexpect, strerror(errno)};
    }

    check_subscribed();

    auto res = co_await async_coro::await_callback_with_result<reactor::connection_state>([this](auto cont) {
      _reactor->continue_after_sent_data(_sock, _subscription_index, std::move(cont));
    });
    if (res == reactor::connection_state::closed) {
      close_connection();
      co_return res_t{unexpect, "Connection was closed"};
    }
  }

  co_return res_t{};
}

async_coro::task<expected<size_t, std::string>> connection::read_buffer(std::span<std::byte> bytes) {  // NOLINT(*-complexity*)
  if (_reactor == nullptr) [[unlikely]] {
    co_return expected<size_t, std::string>{unexpect, "Connection was already closed"};
  }

  if (_ssl) {
    while (true) {
      const auto received = _ssl.read(bytes);
      if (received > 0) {
        co_return size_t(received);
      }
      const auto error = _ssl.get_error(received);

      if (error == ssl_error::connection_was_closed) {
        close_connection();
        co_return 0;
      }

      if (error == ssl_error::wants_read && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        check_subscribed();

        auto res = co_await async_coro::await_callback_with_result<reactor::connection_state>([this](auto cont) {
          _reactor->continue_after_receive_data(_sock, _subscription_index, std::move(cont));
        });
        if (res == reactor::connection_state::closed) {
          close_connection();
          co_return 0;
        }
      } else if (error == ssl_error::wants_write && (errno == EAGAIN || errno == EWOULDBLOCK)) {
        check_subscribed();

        auto res = co_await async_coro::await_callback_with_result<reactor::connection_state>([this](auto cont) {
          _reactor->continue_after_sent_data(_sock, _subscription_index, std::move(cont));
        });
        if (res == reactor::connection_state::closed) {
          close_connection();
          co_return 0;
        }
      } else {
        co_return expected<size_t, std::string>{unexpect, ssl_context::get_ssl_error()};
      }
    }

    co_return 0;
  }

  const auto fd_id = _sock.get_platform_id();

  while (true) {
    const auto received = ::recv(fd_id, bytes.data(), bytes.size(), 0);
    if (received > 0) {
      co_return size_t(received);
    }

    if (received == 0) {
      // connection was closed by peer
      close_connection();
      co_return 0;
    }

    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      check_subscribed();

      auto res = co_await async_coro::await_callback_with_result<reactor::connection_state>([this](auto cont) {
        _reactor->continue_after_receive_data(_sock, _subscription_index, std::move(cont));
      });
      if (res == reactor::connection_state::closed) {
        close_connection();
        co_return 0;
      }
    } else {
      co_return expected<size_t, std::string>{unexpect, strerror(errno)};
    }
  }

  co_return 0;
}

void connection::check_subscribed() {
  if (_subscription_index == k_invalid_index) {
    _subscription_index = _reactor->add_connection(_sock);
  }
}

}  // namespace server::socket_layer
