#pragma once

#include <async_coro/config.h>
#include <async_coro/thread_safety/spin_lock_mutex.h>
#include <async_coro/thread_safety/unique_lock.h>

#include <array>
#include <atomic>
#include <memory>
#include <type_traits>
#include <vector>

namespace async_coro {

/**
 * @brief A thread safe queue that uses atomics for a fast path.
 * In case when there is no preallocated values it will fall back to using spin lock mutex to allocate a new bank of values.
 * This queue is designed to be used in multi producer/multi consumer scenarios.
 * @tparam T The type of the values to be stored in the queue.
 * @tparam BlockSize The number of values to be preallocated in a single bank.
 */
template <typename T, std::uint32_t BlockSize = 64>  // NOLINT(*-magic-*)
class atomic_queue {
  static_assert(BlockSize > 1, "Block size should be more than 1");

  union union_store {
    T value;
    char tmp_byte;

    union_store() noexcept {}  // NOLINT(*member-init)
    union_store(const union_store&) = delete;
    union_store(union_store&&) = delete;
    ~union_store() noexcept {}

    union_store& operator=(const union_store&) = delete;
    union_store& operator=(union_store&&) = delete;
  };

  struct value {
    union_store val;
    value* next;
  };

  struct values_bank {
    std::array<value, BlockSize> values;

    values_bank() noexcept {
      value* prev_value = nullptr;
      for (auto& val : values) {
        if (prev_value) {
          prev_value->next = &val;
        }
        prev_value = &val;
      }
      prev_value->next = nullptr;
    }
  };

 public:
  /**
   * @brief Construct a new atomic queue object
   */
  atomic_queue() noexcept = default;

  atomic_queue(const atomic_queue&) = delete;
  atomic_queue(atomic_queue&&) = delete;

  /**
   * @brief Destroy the atomic queue object and all the values that are still in it.
   */
  ~atomic_queue() noexcept = default;

  atomic_queue& operator=(const atomic_queue&) = delete;
  atomic_queue& operator=(atomic_queue&&) = delete;

  /**
   * @brief Pushes a new value to the queue.
   * In case when there is no preallocated values it will allocate a new bank of values.
   * This can cause a small overhead.
   * @tparam U The types of the arguments to be forwarded to the constructor of T.
   * @param v The arguments to be forwarded to the constructor of T.
   */
  template <typename... U>
  void push(U&&... val) {
    value* head;  // NOLINT(*-init-*)

    {
      unique_lock lock{_free_value_mutex};

      if (!_free_value) {
        allocate_new_bank();
      }

      head = _free_value;
      _free_value = head->next;
    }

    new (std::addressof(head->val.value)) T{std::forward<U>(val)...};
    head->next = nullptr;

    unique_lock lock{_value_mutex};

    value* expected_to_set = nullptr;
    _head.compare_exchange_strong(expected_to_set, head, std::memory_order::relaxed);
    if (_last) {
      _last->next = head;
    }
    _last = head;
  }

  /**
   * @brief Tries to push a new value to the queue.
   * This method will not allocate a new bank of values if there are no preallocated values.
   * @tparam U The types of the arguments to be forwarded to the constructor of T.
   * @param v The arguments to be forwarded to the constructor of T.
   * @return true if the value was pushed successfully, false otherwise.
   */
  template <typename... U>
  bool try_push(U&&... val) noexcept(std::is_nothrow_constructible_v<T, U&&...>) {
    value* head;  // NOLINT(*-init-*)

    {
      unique_lock lock{_free_value_mutex};

      if (!_free_value) {
        return false;
      }

      head = _free_value;
      _free_value = head->next;
    }

    new (std::addressof(head->val.value)) T{std::forward<U>(val)...};
    head->next = nullptr;

    unique_lock lock{_value_mutex};

    value* expected_to_set = nullptr;
    _head.compare_exchange_strong(expected_to_set, head, std::memory_order::relaxed);
    if (_last) {
      _last->next = head;
    }
    _last = head;
  }

  /**
   * @brief Tries to pop a value from the queue.
   * @param v The reference to the value where the popped value will be moved to.
   * @return true if a value was popped successfully, false otherwise.
   */
  bool try_pop(T& val) noexcept(std::is_nothrow_move_assignable_v<T>) {
    value* head;  // NOLINT(*-init-*)

    {
      // we use common lock here as head->next can change unexpectedly
      unique_lock lock{_value_mutex};

      head = _head.load(std::memory_order::relaxed);
      if (head) {
        _head.store(head->next, std::memory_order::relaxed);
        if (_last == head) {
          _last = head->next;
        }
      } else {
        return false;
      }
    }

    val = std::move(head->val.value);
    std::destroy_at(std::addressof(head->val.value));

    {
      unique_lock lock{_free_value_mutex};
      head->next = _free_value;
      _free_value = head;
    }

    return true;
  }

  /**
   * @brief Checks if the queue has any values.
   * @return true if the queue has any values, false otherwise.
   */
  [[nodiscard]] bool has_value() const noexcept {
    return _head.load(std::memory_order::relaxed) != nullptr;
  }

 private:
  void allocate_new_bank() CORO_THREAD_REQUIRES(_free_value_mutex) {
    _additional_banks.emplace_back(std::make_unique<values_bank>());
    _free_value = std::addressof(_additional_banks.back()->values[0]);
  }

 private:
  spin_lock_mutex _free_value_mutex;

  values_bank _head_bank;
  value* _free_value CORO_THREAD_GUARDED_BY(_free_value_mutex) = std::addressof(_head_bank.values[0]);

  spin_lock_mutex _value_mutex;
  value* _last CORO_THREAD_GUARDED_BY(_value_mutex) = nullptr;

  std::vector<std::unique_ptr<values_bank>> _additional_banks CORO_THREAD_GUARDED_BY(_free_value_mutex);

  // moved to end to minify false sharing effects with other atomics
  std::atomic<value*> _head = nullptr;
};

};  // namespace async_coro
