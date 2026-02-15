#pragma once

#include <string_view>

namespace server {

struct static_string {
  std::string_view str;
};

}  // namespace server
