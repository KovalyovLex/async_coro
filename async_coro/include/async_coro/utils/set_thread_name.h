#pragma once

#include <string>
#include <thread>

namespace async_coro {

/**
 * @brief Sets the name of a thread for debugging purposes
 *
 * Attempts to set the thread name using platform-specific APIs.
 * This is useful for debugging and profiling tools.
 *
 * @param thread Reference to the thread whose name should be set
 * @param name The name to assign to the thread
 *
 * @note This is a platform-dependent operation and may not work on all systems
 */
void set_thread_name(std::thread &thread, const std::string &name);

}  // namespace async_coro
