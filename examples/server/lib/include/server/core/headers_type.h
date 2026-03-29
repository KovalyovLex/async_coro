#pragma once

#include <server/utils/ci_string_view.h>

#include <string_view>
#include <utility>
#include <vector>

namespace server::core {

using headers_type = std::vector<std::pair<ci_string_view, std::string_view>>;

}
