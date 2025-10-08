#pragma once

namespace async_coro::internal {

class scheduled_run_data {
 public:
  bool external_continuation_request = false;
};

}  // namespace async_coro::internal
