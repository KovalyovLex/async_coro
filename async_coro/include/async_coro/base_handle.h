#pragma once

#include <async_coro/config.h>

#include <coroutine>
#include <cstdint>
#include <thread>

namespace async_coro {
enum class coroutine_state : std::uint8_t {
  created,
  running,
  suspended,
  finished,
};

class scheduler;

class base_handle {
  friend scheduler;

 public:
  scheduler& get_scheduler() noexcept {
    ASYNC_CORO_ASSERT(_scheduler != nullptr);
    return *_scheduler;
  };

  const scheduler& get_scheduler() const noexcept {
    ASYNC_CORO_ASSERT(_scheduler != nullptr);
    return *_scheduler;
  };

  bool is_current_thread_same() const noexcept {
    return _execution_thread == std::this_thread::get_id();
  }

 protected:
  void init_promise(std::coroutine_handle<> h) noexcept { _handle = h; }

 private:
  base_handle* _parent = nullptr;
  scheduler* _scheduler = nullptr;
  std::coroutine_handle<> _handle;
  std::thread::id _execution_thread = {};
  coroutine_state _state = coroutine_state::created;
};
}  // namespace async_coro
