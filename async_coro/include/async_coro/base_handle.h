#pragma once

#include <async_coro/callback.h>
#include <async_coro/config.h>
#include <async_coro/execution_queue_mark.h>

#include <atomic>
#include <coroutine>
#include <cstdint>
#include <thread>

namespace async_coro {

enum class coroutine_state : std::uint8_t {
  created = 0,
  running,
  suspended,
  waiting_switch,
  finished
};

class scheduler;

class base_handle {
  friend scheduler;

 public:
  base_handle() noexcept
      : _parent(nullptr),
        _is_initialized(false),
        _is_result(false) {}
  base_handle(const base_handle&) = delete;
  base_handle(base_handle&&) = delete;
  ~base_handle() noexcept;

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

  bool is_coro_embedded() const noexcept { return is_embedded(); }

  bool is_finished_acquire() const noexcept { return get_coroutine_state(std::memory_order::acquire) == coroutine_state::finished; }

  bool is_finished() const noexcept { return get_coroutine_state(std::memory_order::relaxed) == coroutine_state::finished; }

  bool is_suspended() const noexcept { return get_coroutine_state() == coroutine_state::suspended; }

  // Should be called on every await_suspend in child coroutines
  void on_suspended() noexcept {
    set_coroutine_state(coroutine_state::suspended);
  }

  // Should be called instead of on_suspended
  void switch_execution_queue(execution_queue_mark execution_queue) noexcept {
    set_coroutine_state(coroutine_state::waiting_switch);
    _execution_queue = execution_queue;
  }

  execution_queue_mark get_execution_queue() const noexcept {
    return _execution_queue;
  }

 protected:
  void init_promise(std::coroutine_handle<> h) noexcept { _handle = h; }

  void on_final_suspend() noexcept {
    set_coroutine_state(coroutine_state::finished, true);
  }

  void on_task_freed_by_scheduler();

  void set_owning_by_task_handle(bool owning);

  callback_base* release_continuation_functor() noexcept {
    return is_embedded() ? nullptr : _continuation.exchange(nullptr, std::memory_order::acquire);
  }

  void set_continuation_functor(callback_base* f) noexcept;

 private:
  void destroy_impl();

  base_handle* get_parent() const noexcept {
    return is_embedded() ? _parent : nullptr;
  }

  void set_parent(base_handle& parent) noexcept {
    _parent = &parent;
    set_embedded(true);
  }

 private:
  static constexpr uint8_t coroutine_state_mask = (1 << 0) | (1 << 1) | (1 << 2);
  static constexpr uint8_t is_embedded_mask = (1 << 3);
  static constexpr uint8_t num_owners_step = (1 << 4);
  static constexpr uint8_t num_owners_mask = (1 << 4) | (1 << 5);

  static constexpr uint8_t get_inverted_mask(uint8_t mask) noexcept {
    return static_cast<uint8_t>(~mask);
  }

  uint8_t dec_num_owners() noexcept;

  void inc_num_owners() noexcept;

  coroutine_state get_coroutine_state(std::memory_order order = std::memory_order::relaxed) const noexcept {
    return static_cast<coroutine_state>(_atomic_state.load(order) & coroutine_state_mask);
  }

  void set_coroutine_state(coroutine_state value, bool release = false) noexcept {
    if (release) {
      update_value(static_cast<uint8_t>(value), get_inverted_mask(coroutine_state_mask), std::memory_order::relaxed, std::memory_order::release);
    } else {
      update_value(static_cast<uint8_t>(value), get_inverted_mask(coroutine_state_mask));
    }
  }

  bool is_embedded() const noexcept {
    return _atomic_state.load(std::memory_order::relaxed) & is_embedded_mask;
  }

  void set_embedded(bool value) noexcept {
    update_value(value ? is_embedded_mask : 0, get_inverted_mask(is_embedded_mask));
  }

  void update_value(const uint8_t value, const uint8_t mask, std::memory_order read = std::memory_order::relaxed, std::memory_order write = std::memory_order::relaxed) noexcept {
    uint8_t expected = _atomic_state.load(read);
    while (!_atomic_state.compare_exchange_weak(expected, (expected & mask) | value, write, read)) {
    }
  }

 private:
  union {
    std::atomic<callback_base*> _continuation;
    base_handle* _parent;
  };

  callback_base::ptr _start_function;
  scheduler* _scheduler = nullptr;
  std::coroutine_handle<> _handle;
  std::thread::id _execution_thread = {};
  execution_queue_mark _execution_queue = execution_queues::main;
  std::atomic<uint8_t> _atomic_state{num_owners_step};  // 1 owner by default

 protected:
  bool _is_initialized : 1;
  bool _is_result : 1;
};

}  // namespace async_coro
