#include "utils/has_open_ssl.h"

#if SERVER_HAS_SSL

#ifdef _WIN32
// avoid all this windows junk from openssl
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#endif

#include <openssl/err.h>
#include <openssl/ssl.h>

#endif

#include <async_coro/thread_safety/mutex.h>
#include <async_coro/thread_safety/unique_lock.h>
#include <server/socket_layer/ssl_context.h>

namespace server::socket_layer {

static bool is_openssl_initialized = false;  // NOLINT(*non-const-*)
static async_coro::mutex init_ssl_mutex{};   // NOLINT(*non-const-*)

ssl_context::ssl_context(const std::string& key_path, const std::string& cert_path,
                         const std::string& ciphers) {
#if SERVER_HAS_SSL
  if (!is_openssl_initialized) {
    async_coro::unique_lock lock{init_ssl_mutex};

    if (!is_openssl_initialized) {
      SSL_load_error_strings();
      OpenSSL_add_ssl_algorithms();
      is_openssl_initialized = true;
    }

    auto* ctx = SSL_CTX_new(TLS_server_method());
    if (ctx == nullptr) {
      perror("Unable to create SSL context");
      ERR_print_errors_fp(stderr);
      return;
    }

    // set min TLS version 1.2
    SSL_CTX_set_min_proto_version(ctx, TLS1_2_VERSION);

    /* Set the ciphersuite if provided */
    if (!ciphers.empty() && SSL_CTX_set_cipher_list(ctx, ciphers.c_str()) <= 0) {
      ERR_print_errors_fp(stderr);
      SSL_CTX_free(ctx);
      return;
    }

    /* Set the key and cert */
    if (SSL_CTX_use_certificate_file(ctx, cert_path.c_str(), SSL_FILETYPE_PEM) <= 0) {
      ERR_print_errors_fp(stderr);
      SSL_CTX_free(ctx);
      return;
    }

    if (SSL_CTX_use_PrivateKey_file(ctx, key_path.c_str(), SSL_FILETYPE_PEM) <= 0) {
      ERR_print_errors_fp(stderr);
      SSL_CTX_free(ctx);
      return;
    }

    _ctx = ctx;
  }
#endif
}

ssl_context::~ssl_context() noexcept {
#if SERVER_HAS_SSL
  if (_ctx != nullptr) {
    auto* ssl_ctx = static_cast<SSL_CTX*>(_ctx);

    SSL_CTX_free(ssl_ctx);
  }
#endif
}

std::string ssl_context::get_ssl_error() {
#if SERVER_HAS_SSL
  BIO* bio = BIO_new(BIO_s_mem());
  ERR_print_errors(bio);

  char* buf = nullptr;
  size_t len = BIO_get_mem_data(bio, &buf);

  std::string res;
  if (len > 0) {
    res.append(buf, len);
  }

  BIO_free(bio);
  return res;
#else
  return "No SSL in this build";
#endif
}

}  // namespace server::socket_layer
