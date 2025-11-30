#pragma once

namespace async_coro {
class base_handle;
}

namespace async_coro::internal {

class scheduled_run_data {
 public:
  base_handle* coroutine_to_run_next = nullptr;
};

}  // namespace async_coro::internal
