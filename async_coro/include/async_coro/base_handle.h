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
  finished
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

  void on_child_coro_added(base_handle& child);

  bool is_coro_embedded() const noexcept { return _parent != nullptr; }

 protected:
  void init_promise(std::coroutine_handle<> h) noexcept { _handle = h; }

  void on_final_suspend() noexcept {
    _state = coroutine_state::finished;
  }

  void try_free_task_impl() {
    if (_handle) {
      _ready_for_destroy.store(true, std::memory_order::relaxed);
      if (_continuation.load(std::memory_order::acquire) == nullptr) {
        bool expected = true;
        if (_ready_for_destroy.compare_exchange_strong(expected, false, std::memory_order::relaxed)) {
          _handle.destroy();
        }
      }
    }
  }

  void set_continuation_impl(void* handle) {
    _continuation.store(handle, std::memory_order::release);

    if (handle == nullptr) {
      bool expected = true;
      if (_ready_for_destroy.compare_exchange_strong(expected, false, std::memory_order::relaxed)) {
        _handle.destroy();
      }
    }
  }

  template <class T>
  T* get_continuation() const noexcept {
    return static_cast<T*>(_continuation.load(std::memory_order::acquire));
  }

 private:
  base_handle* _parent = nullptr;
  scheduler* _scheduler = nullptr;
  std::coroutine_handle<> _handle;
  std::thread::id _execution_thread = {};
  std::atomic<void*> _continuation = nullptr;
  std::atomic_bool _ready_for_destroy = false;
  coroutine_state _state = coroutine_state::created;
};
}  // namespace async_coro
