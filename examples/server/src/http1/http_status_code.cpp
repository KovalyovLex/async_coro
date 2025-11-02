#include <server/http1/http_status_code.h>

namespace server::http1 {

std::string_view as_string(status_code met) noexcept {
  switch (met) {
    case status_code::Ok:
      return "200 OK";
    case status_code::Created:
      return "201 Created";
    case status_code::Accepted:
      return "202 Accepted";
    case status_code::NoContent:
      return "204 No Content";
    case status_code::PartialContent:
      return "206 Partial Content";
    case status_code::MultipleChoices:
      return "300 Multiple Choices";
    case status_code::MovedPermanently:
      return "301 Moved Permanently";
    case status_code::Found:
      return "302 Found";
    case status_code::NotModified:
      return "304 Not Modified";
    case status_code::TemporaryRedirect:
      return "307 Temporary Redirect";
    case status_code::BadRequest:
      return "400 Bad Request";
    case status_code::Unauthorized:
      return "401 Unauthorized";
    case status_code::Forbidden:
      return "403 Forbidden";
    case status_code::NotFound:
      return "404 Not Found";
    case status_code::MethodNotAllowed:
      return "405 Method Not Allowed";
    case status_code::RequestTimeout:
      return "408 Request Timeout";
    case status_code::InternalServerError:
      return "500 Internal Server Error";
    case status_code::NotImplemented:
      return "501 Not Implemented";
    case status_code::BadGateway:
      return "502 Bad Gateway";
    case status_code::ServiceUnavailable:
      return "503 Service Unavailable";
    case status_code::GatewayTimeout:
      return "504 Gateway Timeout";
    case status_code::HttpVersionNotSupported:
      return "505 Http Version Not Supported";
  }
}

};  // namespace server::http1
