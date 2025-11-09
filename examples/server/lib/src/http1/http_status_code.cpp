#include <server/http1/http_status_code.h>

namespace server::http1 {

std::string_view as_string(status_code met) noexcept {
  switch (met) {
    case status_code::Ok:
      return "OK";
    case status_code::SwitchingProtocols:
      return "Switching Protocols";
    case status_code::Created:
      return "Created";
    case status_code::Accepted:
      return "Accepted";
    case status_code::NoContent:
      return "No Content";
    case status_code::PartialContent:
      return "Partial Content";
    case status_code::MultipleChoices:
      return "Multiple Choices";
    case status_code::MovedPermanently:
      return "Moved Permanently";
    case status_code::Found:
      return "Found";
    case status_code::NotModified:
      return "Not Modified";
    case status_code::TemporaryRedirect:
      return "Temporary Redirect";
    case status_code::BadRequest:
      return "Bad Request";
    case status_code::Unauthorized:
      return "Unauthorized";
    case status_code::Forbidden:
      return "Forbidden";
    case status_code::NotFound:
      return "Not Found";
    case status_code::MethodNotAllowed:
      return "Method Not Allowed";
    case status_code::RequestTimeout:
      return "Request Timeout";
    case status_code::LengthRequired:
      return "Length Required";
    case status_code::UpgradeRequired:
      return "Upgrade Required";
    case status_code::InternalServerError:
      return "Internal Server Error";
    case status_code::NotImplemented:
      return "Not Implemented";
    case status_code::BadGateway:
      return "Bad Gateway";
    case status_code::ServiceUnavailable:
      return "Service Unavailable";
    case status_code::GatewayTimeout:
      return "Gateway Timeout";
    case status_code::HttpVersionNotSupported:
      return "Http Version Not Supported";
  }
  return {};
}

};  // namespace server::http1
