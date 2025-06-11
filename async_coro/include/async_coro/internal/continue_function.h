#pragma once

namespace async_coro::internal {

class continue_function_base {
 public:
  continue_function_base() noexcept = default;
  continue_function_base(const continue_function_base&) noexcept = default;
  continue_function_base(continue_function_base&&) noexcept = default;

  virtual ~continue_function_base() noexcept = default;
};

template <class T>
class continue_function : public continue_function_base {
 public:
  virtual void execute(T) noexcept = 0;
};

}  // namespace async_coro::internal