#pragma once

#include <server/io/io_config.h>

#include <cstdint>

namespace server::io {

/**
 * @brief Socket transport type enumeration.
 *
 * Provides user-friendly names for socket kinds used throughout the IOCP reactor.
 */
enum class socket_type_id : uint8_t {
  tcp,  // TCP (SOCK_STREAM)
  udp,  // UDP (SOCK_DGRAM)
};

}  // namespace server::io
