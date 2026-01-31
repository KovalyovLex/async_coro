#pragma once

#include <async_coro/config.h>
#include <async_coro/utils/callback_fwd.h>
#include <async_coro/utils/callback_ptr.h>

#include <cstddef>
#include <tuple>
#include <utility>

namespace async_coro::internal {

class continue_callback_holder;

// Callback type to store on stack. For use in continue_after_complete method for advanced awaiters
using continue_callback = callback<std::tuple<continue_callback_holder, bool>(bool)>;

using continue_callback_ptr = callback_ptr<std::tuple<continue_callback_holder, bool>(bool)>;

using continue_callback_atomic_ptr = callback_atomic_ptr<std::tuple<continue_callback_holder, bool>(bool)>;

// RAII holder of callback
class continue_callback_holder : public callback_ptr<std::tuple<continue_callback_holder, bool>(bool)> {
 public:
  using ptr = callback_ptr<std::tuple<continue_callback_holder, bool>(bool)>;
  using atomic_ptr = callback_atomic_ptr<std::tuple<continue_callback_holder, bool>(bool)>;

  continue_callback_holder() noexcept : ptr(nullptr) {}
  continue_callback_holder(std::nullptr_t) noexcept : ptr(nullptr) {}  // NOLINT(*explicit*)

  continue_callback_holder(ptr clb) noexcept  // NOLINT(*explicit*)
      : ptr(std::move(clb)) {
  }

  continue_callback_holder(atomic_ptr clb) noexcept  // NOLINT(*explicit*)
      : ptr(clb.release()) {
  }
};

}  // namespace async_coro::internal
