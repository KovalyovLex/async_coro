#pragma once

namespace async_coro::internal {

class scheduled_run_data {
 public:
  bool external_continuation_request = false;
  bool continue_parent_on_finish = false;
};

}  // namespace async_coro::internal
