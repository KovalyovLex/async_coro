#pragma once

#include <async_coro/task.h>
#include <server/core/error.h>
#include <server/utils/expected.h>

#include <cstddef>
#include <span>

namespace server::core {

class i_write_connection {
 public:
  i_write_connection() noexcept = default;
  i_write_connection(const i_write_connection&) = delete;
  i_write_connection(i_write_connection&&) = default;

  virtual ~i_write_connection() noexcept;

  i_write_connection& operator=(const i_write_connection&) = delete;
  i_write_connection& operator=(i_write_connection&&) = default;

  [[nodiscard]] virtual bool is_closed() const noexcept = 0;

  virtual void close_connection() = 0;

  [[nodiscard]] virtual async_coro::task<expected<void, error>> write_buffer(std::span<const std::byte> bytes) = 0;
};

}  // namespace server::core
