#pragma once

#include <async_coro/task.h>
#include <server/http1/client_request.h>
#include <server/http1/client_response.h>
#include <server/http1/forwarding_params.h>
#include <server/utils/expected.h>

namespace server::core {
class i_write_connection;
class i_read_connection;
}  // namespace server::core

namespace server::http1 {

class request;

// Thin helper that knows how to perform a request/response exchange over an
// existing `socket_layer::connection`.  It is intentionally minimal: the
// caller manages connection lifetime, timeouts, and retries, which makes it
// suitable for building proxy logic where the same connection object is used
// in both directions.
class http_client {
 public:
  explicit http_client(http_version ver = http_version::http_1_1) noexcept : _ver(ver) {}

  http_client(const http_client&) = delete;
  http_client(http_client&&) = default;
  http_client& operator=(const http_client&) = delete;
  http_client& operator=(http_client&&) = default;
  ~http_client() = default;

  // Perform the round-trip.  On success the response object is returned
  // (already parsed).  The connection may be reused for another request if
  // the server supports keep-alive.
  auto send_request(client_request& req, server::core::i_write_connection& write, server::core::i_read_connection& read) -> async_coro::task<expected<client_response, std::string>>;  // NOLINT(cppcoreguidelines-avoid-reference-coroutine-parameters)

  /**
   * @brief Forward an existing server-side request using RFC 7239 Forwarded header.
   *
   * Transforms a server-side request into a client request suitable for forwarding to
   * an upstream server. Adds a standardized Forwarded header containing proxy information
   * per RFC 7239.
   *
   * Behavior:
   * - Removes all X-Forwarded-* headers (non-compliant with RFC 7239)
   * - If multiple Forwarded headers exist, combines them (comma-separated)
   * - Appends a new proxy element representing this proxy
   * - Result format: `Forwarded: by=X;for=Y;proto=Z, by=A;proto=W, ...`
   *   (parameters within an element separated by `;`, elements separated by `,`)
   *
   * Parameters from forwarding_params:
   * - 'by': This proxy's address (IP or hostname)
   * - 'for': Remote client's address (IP or hostname)
   * - 'proto': Protocol used by client to connect (http/https, defaults to "https")
   * Omitted parameters are not included in the generated header.
   *
   * @param orig The server-side request to forward (will be moved)
   * @param params Forwarding parameters. Defaults to empty by/for_addr and "https" proto.
   * @return A client_request ready to be sent upstream
   */
  static client_request forward_request(request&& orig, const forwarding_params& params = {});

 private:
  http_version _ver;
};

}  // namespace server::http1
