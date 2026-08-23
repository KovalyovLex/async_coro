#pragma once

#include <server/io/io_config.h>

namespace server::io {

/**
 * @brief Closes a socket descriptor.
 *
 * Closes the given socket and releases the associated resource.
 *
 * @param socket_id The socket to close.
 * @return true if the socket was closed successfully or was already invalid.
 * @return false if the close operation failed.
 */
bool close_socket(socket_type socket_id) noexcept;

/**
 * @brief Closes a file handle/descriptor.
 *
 * Closes the given file handle and releases the associated resource.
 *
 * @param handle The file handle to close.
 * @return true if the handle was closed successfully or was already invalid.
 * @return false if the close operation failed.
 */
bool close_file(file_handle_t handle) noexcept;

}  // namespace server::io
