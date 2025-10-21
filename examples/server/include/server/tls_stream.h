// Minimal TLS stream header placeholder
#pragma once

namespace server {

struct TlsConfig {
  const char* cert_file = nullptr;
  const char* key_file = nullptr;
};

}  // namespace server
