#pragma once

#include <string>

namespace server {

struct ssl_config {
  std::string key_path;
  std::string cert_path;
  std::string ciphers;
};

}  // namespace server
