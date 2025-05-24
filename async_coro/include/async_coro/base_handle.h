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
  base_handle() noexcept
      : _parent(nullptr),
        _state(coroutine_state::created),
        _is_embedded(false),
        _has_handle(false),
        _is_initialized(false),
        _is_result(false) {}
  base_handle(const base_handle&) = delete;
  base_handle(base_handle&&) = delete;
  ~base_handle() noexcept = default;

  scheduler& get_scheduler() noexcept {
    ASYNC_CORO_ASSERT(_scheduler != nullptr);
    return *_scheduler;
  }

  const scheduler& get_scheduler() const noexcept {
    ASYNC_CORO_ASSERT(_scheduler != nullptr);
    return *_scheduler;
  }

  bool is_current_thread_same() const noexcept {
    return _execution_thread == std::this_thread::get_id();
  }

  void on_child_coro_added(base_handle& child);

  bool is_coro_embedded() const noexcept { return _is_embedded; }

 protected:
  void init_promise(std::coroutine_handle<> h) noexcept { _handle = h; }

  void on_final_suspend() noexcept {
    _state = coroutine_state::finished;
  }

  void try_free_task_impl() {
    _ready_for_destroy.store(true, std::memory_order::relaxed);
    if (is_coro_embedded() || get_task_handle<void>() == nullptr) {
      destroy_impl();
    }
  }

  void set_task_handle_impl(void* handle) {
    ASYNC_CORO_ASSERT(!is_coro_embedded());

    _has_handle = true;
    _task_handle.store(handle, std::memory_order::release);

    if (handle == nullptr) {
      destroy_impl();
    }
  }

  template <class T>
  T* get_task_handle() const noexcept {
    return _has_handle ? static_cast<T*>(_task_handle.load(std::memory_order::acquire)) : nullptr;
  }

 private:
  void destroy_impl() {
    bool expected = true;
    if (_ready_for_destroy.compare_exchange_strong(expected, false, std::memory_order::relaxed)) {
      const auto handle = _handle;
      _handle = {};
      handle.destroy();
    }
  }

  base_handle* get_parent() const noexcept {
    return _is_embedded ? _parent : nullptr;
  }

  void set_parent(base_handle& parent) noexcept {
    _parent = &parent;
    _is_embedded = true;
  }

 private:
  union {
    std::atomic<void*> _task_handle;
    base_handle* _parent;
  };

  scheduler* _scheduler = nullptr;
  std::coroutine_handle<> _handle;
  std::thread::id _execution_thread = {};
  std::atomic_bool _ready_for_destroy = false;
  coroutine_state _state : 2;
  bool _is_embedded : 1;
  bool _has_handle : 1;

 protected:
  bool _is_initialized : 1;
  bool _is_result : 1;
};

}  // namespace async_coro
