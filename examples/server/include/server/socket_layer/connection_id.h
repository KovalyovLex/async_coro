#pragma once

#include <compare>

#include "socket_config.h"

namespace server::socket_layer {

class connection_id {
 public:
  explicit constexpr connection_id(socket_type fd_id) noexcept
      : _fid(fd_id) {}

  [[nodiscard]] constexpr auto get_platform_id() const noexcept { return _fid; }

  constexpr auto operator<=>(const connection_id& other) const noexcept = default;

 private:
  socket_type _fid;
};

inline constexpr auto invalid_connection = connection_id{invalid_socket_id};

}  // namespace server::socket_layer
