#pragma once

#include <async_coro/utils/function_view.h>
#include <server/core/headers_type.h>

#include <cstddef>
#include <string_view>

namespace server::http1 {

/**
 * @brief Base class that encapsulates common header management functionality.
 *
 * Provides protected access to the headers collection and public methods for querying headers
 * using linear search. Subclasses can override methods for optimized search strategies.
 */
class headers_holder {
 protected:
  ~headers_holder() noexcept = default;

 public:
  headers_holder() noexcept = default;
  headers_holder(const headers_holder&) = delete;
  headers_holder(headers_holder&&) noexcept = default;

  headers_holder& operator=(const headers_holder&) = delete;
  headers_holder& operator=(headers_holder&&) noexcept = default;

  /**
   * @brief Find the first header with the given name using linear search.
   *
   * @param name The header name to search for (case-insensitive).
   * @return Pointer to the header pair if found, nullptr otherwise.
   */
  [[nodiscard]] const core::headers_type::value_type* find_header(std::string_view name) const noexcept;

  /**
   * @brief Iterate through all headers with the given name, passing only the value to the callback.
   *
   * @param name The header name to search for (case-insensitive).
   * @param func Callback function that receives the header value as std::string_view.
   */
  void foreach_header_with_name(std::string_view name, async_coro::function_view<void(std::string_view)> func) const;

  /**
   * @brief Iterate through all headers with the given name (noexcept), passing only the value to the callback.
   *
   * @param name The header name to search for (case-insensitive).
   * @param func Noexcept callback function that receives the header value as std::string_view.
   */
  void foreach_header_with_name_noexcept(std::string_view name, async_coro::function_view<void(std::string_view) noexcept> func) const noexcept;

  /**
   * @brief Check if there is a header with the given name that contains the given value.
   *
   * Useful for checking values in comma-separated or space-separated header fields
   * (e.g., "gzip" in "Accept-Encoding: gzip, deflate").
   *
   * @param name The header name (case-insensitive).
   * @param value The value to search for within the header.
   * @return True if the value is found as a complete token in the header.
   */
  [[nodiscard]] bool has_value_in_header(std::string_view name, std::string_view value) const noexcept;

  /**
   * @brief Get the total number of headers.
   *
   * @return Number of headers in the collection.
   */
  [[nodiscard]] size_t get_number_of_headers() const noexcept { return _headers.size(); }

  /**
   * @brief Get a const reference to the headers collection.
   *
   * @return Const reference to the underlying headers vector.
   */
  [[nodiscard]] const core::headers_type& get_headers() const noexcept { return _headers; }

 protected:
  core::headers_type _headers;
};

}  // namespace server::http1
