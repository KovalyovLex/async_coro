#pragma once

#include <thread>

namespace async_coro {
class base_handle;
}

namespace async_coro::internal {

class scheduled_run_data {
 public:
  scheduled_run_data() noexcept : _owner_thread(std::this_thread::get_id()) {}

  base_handle* coroutine_to_run_next = nullptr;

  [[nodiscard]] bool is_same_owner_thread(const scheduled_run_data& other) const noexcept {
    return _owner_thread == other._owner_thread;
  }

 private:
  std::thread::id _owner_thread;
};

}  // namespace async_coro::internal
