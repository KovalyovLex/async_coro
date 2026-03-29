#pragma once

#include <string>
#include <string_view>

namespace server::http1 {

/**
 * @brief Parameters for forwarding requests according to RFC 7239.
 *
 * Used with http_client::forward_request() to add a standardized Forwarded header
 * instead of non-standard X-Forwarded-* headers. Each parameter is optional; omitted
 * parameters will not appear in the generated Forwarded header.
 *
 * RFC 7239 format: Forwarded: by=<identifier>;for=<identifier>;proto=<protocol>, ...
 * (parameters separated by semicolons within an element, elements separated by commas)
 */
struct forwarding_params {
  /// This proxy's IP address or hostname (used in 'by' field)
  std::string_view by = {};

  /// Protocol used by the client to connect to this proxy (http/https)
  std::string_view proto = "https";

  /// Remote client's IP address or hostname (used in 'for' field)
  std::string_view for_addr = {};

  /**
   * @brief Formats and appends a proxy element to an existing Forwarded header (RFC 7239).
   *
   * Creates an RFC 7239-compliant proxy element with the current parameters and appends
   * it to the existing Forwarded header value if present.
   *
   * RFC 7239 format:
   * - Parameters within a proxy element are separated by semicolons: `by=X;for=Y;proto=Z`
   * - Multiple proxy elements are separated by commas: `by=X;proto=Z, by=A;proto=W`
   *
   * Examples:
   *   - New element with existing header:
   *     Input: `by=192.0.2.1;proto=http`, params{by="192.0.2.2", proto="https"}
   *     Output: `by=192.0.2.1;proto=http, by=192.0.2.2;proto=https`
   *   - New element without existing header:
   *     Input: "", params{by="192.0.2.2", for_addr="198.51.100.1", proto="https"}
   *     Output: `by=192.0.2.2;for=198.51.100.1;proto=https`
   *
   * @param existing_header Existing Forwarded header value (may be empty or contain multiple elements)
   * @return Complete Forwarded header value with the new element appended (or just the new element if existing_header is empty).
   *         Returns empty string if both existing_header and all parameters are empty.
   */
  [[nodiscard]] std::string format_proxy_element(std::string_view existing_header) const;
};

}  // namespace server::http1
