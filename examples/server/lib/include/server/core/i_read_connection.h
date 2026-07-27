#pragma once

#include <async_coro/task.h>
#include <server/utils/expected.h>

#include <cstddef>
#include <span>
#include <string>

namespace server::core {

class i_read_connection {
 public:
  i_read_connection() noexcept = default;
  i_read_connection(const i_read_connection&) = delete;
  i_read_connection(i_read_connection&&) = default;

  virtual ~i_read_connection() noexcept;

  i_read_connection& operator=(const i_read_connection&) = delete;
  i_read_connection& operator=(i_read_connection&&) = default;

  [[nodiscard]] virtual bool is_closed() const noexcept = 0;

  [[nodiscard]] virtual async_coro::task<expected<size_t, std::string>> read_buffer(std::span<std::byte> bytes) = 0;
};

}  // namespace server::core
