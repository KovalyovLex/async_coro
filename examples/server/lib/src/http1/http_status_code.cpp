#include <server/http1/http_status_code.h>

namespace server::http1 {

std::string_view as_string(status_code met) noexcept {
  switch (met) {
    case status_code::ok:
      return "OK";
    case status_code::switching_protocols:
      return "Switching Protocols";
    case status_code::created:
      return "Created";
    case status_code::accepted:
      return "Accepted";
    case status_code::no_content:
      return "No Content";
    case status_code::partial_content:
      return "Partial Content";
    case status_code::multiple_choices:
      return "Multiple Choices";
    case status_code::moved_permanently:
      return "Moved Permanently";
    case status_code::found:
      return "Found";
    case status_code::not_modified:
      return "Not Modified";
    case status_code::temporary_redirect:
      return "Temporary Redirect";
    case status_code::bad_request:
      return "Bad Request";
    case status_code::unauthorized:
      return "Unauthorized";
    case status_code::forbidden:
      return "Forbidden";
    case status_code::not_found:
      return "Not Found";
    case status_code::method_not_allowed:
      return "Method Not Allowed";
    case status_code::request_timeout:
      return "Request Timeout";
    case status_code::length_required:
      return "Length Required";
    case status_code::upgrade_required:
      return "Upgrade Required";
    case status_code::internal_server_error:
      return "Internal Server Error";
    case status_code::not_implemented:
      return "Not Implemented";
    case status_code::bad_gateway:
      return "Bad Gateway";
    case status_code::service_unavailable:
      return "Service Unavailable";
    case status_code::gateway_timeout:
      return "Gateway Timeout";
    case status_code::http_version_not_supported:
      return "Http Version Not Supported";
  }
  return {};
}

};  // namespace server::http1
