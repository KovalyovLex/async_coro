#pragma once

#include <server/io/io_config.h>

#include <compare>

namespace server::socket_layer {

class connection_id {
 public:
  explicit constexpr connection_id(io::socket_type fd_id) noexcept
      : _fid(fd_id) {}

  [[nodiscard]] constexpr auto get_platform_id() const noexcept { return _fid; }

  constexpr auto operator<=>(const connection_id& other) const noexcept = default;

 private:
  io::socket_type _fid;
};

inline constexpr auto invalid_connection = connection_id{io::invalid_socket_id};

}  // namespace server::socket_layer
