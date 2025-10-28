
#include <async_coro/await/await_callback.h>
#include <async_coro/config.h>
#include <async_coro/utils/passkey.h>
#include <openssl/ssl.h>
#include <server/socket_layer/connection.h>
#include <server/socket_layer/reactor.h>
#include <server/socket_layer/ssl_connection.h>
#include <server/socket_layer/ssl_context.h>
#include <server/utils/expected.h>

#include <cstddef>
#include <limits>

namespace server::socket_layer {

ssl_connection::ssl_connection(ssl_context& context, socket_layer::connection_id conn) {
  auto* ssl_cnt = static_cast<SSL_CTX*>(context.get_context(async_coro::passkey{this}));
  auto* ssl = SSL_new(ssl_cnt);

  SSL_set_fd(ssl, conn.get_platform_id());

  _ssl = ssl;
}

ssl_connection::~ssl_connection() noexcept {
  if (_ssl != nullptr) {
    auto* ssl = static_cast<SSL*>(_ssl);

    SSL_shutdown(ssl);
    SSL_free(ssl);
  }
}

ssl_error ssl_connection::get_error(int result) const noexcept {
  const int err = SSL_get_error(static_cast<SSL*>(_ssl), result);

  if (err == SSL_ERROR_WANT_WRITE) {
    return ssl_error::wants_write;
  }
  if (err == SSL_ERROR_WANT_READ) {
    return ssl_error::wants_read;
  }
  if (err == SSL_ERROR_ZERO_RETURN) {
    return ssl_error::connection_was_closed;
  }
  if (err == SSL_ERROR_NONE) {
    return ssl_error::no_error;
  }

  return ssl_error::other_error;
}

async_coro::task<expected<bool, std::string>> ssl_connection::handshake(connection& connection) {  // NOLINT(*-reference*)
  ASYNC_CORO_ASSERT(_ssl != nullptr);

  auto* ssl = static_cast<SSL*>(_ssl);

  while (int ret = SSL_accept(ssl)) {
    if (ret == 1) {
      co_return expected<bool, std::string>{true};
    }

    if (ret == 2) {
      // was shutdown by peer
      co_return expected<bool, std::string>{false};
    }

    const int err = SSL_get_error(ssl, ret);

    if (err == SSL_ERROR_ZERO_RETURN) {
      // was shutdown by peer
      co_return expected<bool, std::string>{false};
    }

    if (err == SSL_ERROR_WANT_WRITE) {
      connection.check_subscribed();

      auto res = co_await async_coro::await_callback_with_result<reactor::connection_state>([&connection](auto cont) {
        connection.get_reactor()->continue_after_sent_data(connection.get_connection_id(), connection.get_subscription_index(), std::move(cont));
      });

      if (res == reactor::connection_state::closed) {
        connection.close_connection();
        co_return expected<bool, std::string>{false};
      }
    } else if (err == SSL_ERROR_WANT_READ) {
      connection.check_subscribed();

      auto res = co_await async_coro::await_callback_with_result<reactor::connection_state>([&connection](auto cont) {
        connection.get_reactor()->continue_after_receive_data(connection.get_connection_id(), connection.get_subscription_index(), std::move(cont));
      });
      if (res == reactor::connection_state::closed) {
        connection.close_connection();
        co_return expected<bool, std::string>{false};
      }
    } else {
      co_return expected<bool, std::string>{unexpect, ssl_context::get_ssl_error()};
    }
  }

  ASYNC_CORO_ASSERT(false && "SSL_accept returned zero");  // NOLINT(*static-assert)

  co_return expected<bool, std::string>{unexpect, ssl_context::get_ssl_error()};
}

int ssl_connection::read(std::span<std::byte> bytes) {
  ASYNC_CORO_ASSERT(size_t(std::numeric_limits<int>::max()) < bytes.size());

  auto* ssl = static_cast<SSL*>(_ssl);

  return SSL_read(ssl, bytes.data(), static_cast<int>(bytes.size()));
}

int ssl_connection::write(std::span<const std::byte> bytes) {
  ASYNC_CORO_ASSERT(size_t(std::numeric_limits<int>::max()) < bytes.size());

  auto* ssl = static_cast<SSL*>(_ssl);

  return SSL_write(ssl, bytes.data(), static_cast<int>(bytes.size()));
}

}  // namespace server::socket_layer
