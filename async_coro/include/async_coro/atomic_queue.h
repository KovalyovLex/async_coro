#pragma once

#include <async_coro/config.h>

#include <array>
#include <atomic>
#include <thread>
#include <type_traits>

namespace async_coro {

template <typename T, uint32_t BlockSize = 64>
class atomic_queue {
  struct task_chunk {
    alignas(alignof(T)) std::array<char[sizeof(T)], BlockSize> values;
    std::atomic<task_chunk*> next = nullptr;
    std::atomic<uint32_t> begin = 0;
    std::atomic<uint32_t> end = 0;
    std::atomic<uint32_t> num_used = 0;
    std::atomic<int32_t> free_protect = 0;
    std::atomic_bool create_next_crit = false;

    template <typename... U>
    void push(atomic_queue& q, U&&... v) noexcept(std::is_nothrow_constructible_v<T, U&&...>) {
      auto cur_index = num_used.fetch_add(1, std::memory_order::relaxed);
      if (cur_index < values.size()) {
        new (&values[cur_index]) T(std::forward<U>(v)...);

        // wait other threads finish write
        auto expected = cur_index;
        while (!end.compare_exchange_strong(expected, cur_index + 1, std::memory_order::release, std::memory_order::relaxed)) {
          std::this_thread::yield();
          expected = cur_index;
        }
        free_protect.fetch_sub(1, std::memory_order::relaxed);
      } else {
        // need to use next chunk
        num_used.fetch_sub(1, std::memory_order::relaxed);
        auto* next_created = q.create_next_chunk(this);
        free_protect.fetch_sub(1, std::memory_order::relaxed);

        auto protect = next_created->free_protect.load(std::memory_order::relaxed);
        do {
          if (protect < 0) {
            // this chunk was freed try next
            next_created = q._head_push.load(std::memory_order::relaxed);
            ASYNC_CORO_ASSERT(next_created != nullptr);
            protect = next_created->free_protect.load(std::memory_order::relaxed);
          }
        } while (!next_created->free_protect.compare_exchange_weak(protect, protect + 1, std::memory_order::relaxed));

        next_created->push(q, std::forward<U>(v)...);
      }
    }

    bool try_pop(T& v, atomic_queue& q, bool& need_release) noexcept(std::is_nothrow_move_assignable_v<T>) {
      auto cur_begin = begin.load(std::memory_order::relaxed);
      if (cur_begin < values.size()) {
        const auto cur_end = end.load(std::memory_order::acquire);
        do {
          if (cur_begin >= cur_end) {
            // no values
            return false;
          }
        } while (!begin.compare_exchange_weak(cur_begin, cur_begin + 1, std::memory_order::relaxed));

        // syncronization here guaranteed by end in release-acquire ordering
        v = std::move(reinterpret_cast<T&>(values[cur_begin]));
        return true;
      } else {
        auto* cur_next = next.load(std::memory_order::relaxed);
        if (cur_next) {
          // current chunk is over
          return q.on_chunk_depleted(v, this, cur_next, need_release);
        }
        return false;
      }
    }

    bool has_value() const noexcept {
      const auto cur_begin = begin.load(std::memory_order::relaxed);
      const auto cur_end = end.load(std::memory_order::relaxed);
      if (cur_begin < values.size()) {
        return cur_begin < cur_end;
      } else {
        auto* cur_next = next.load(std::memory_order::relaxed);
        return cur_next && cur_next->has_value();
      }
    }
  };

  bool on_chunk_depleted(T& v, task_chunk* c, task_chunk* cur_next, bool& need_release) noexcept {
    // head will be next
    task_chunk* expected = c;
    if (!_head_pop.compare_exchange_strong(expected, cur_next, std::memory_order::relaxed)) {
      if (expected == cur_next) {
        // freed by someone else
        need_release = false;
        c->free_protect.fetch_sub(1, std::memory_order::relaxed);
        return this->try_pop(v);
      }
      return false;
    }
    // only one thread can pass here

    need_release = false;

    // wait till other threads finish push\pop and block future work
    int32_t expected_protects = 1;
    while (!c->free_protect.compare_exchange_strong(expected_protects, -1, std::memory_order::relaxed)) {
      expected_protects = 1;
      std::this_thread::yield();
    }

    // push c to list of free chunks
    expected = _free_chain.load(std::memory_order::relaxed);

    ASYNC_CORO_ASSERT(c->next.load(std::memory_order::relaxed) == cur_next);

    c->next.store(expected, std::memory_order::relaxed);

    while (!_free_chain.compare_exchange_strong(expected, c, std::memory_order::relaxed)) {
      c->next.store(expected, std::memory_order::relaxed);
    }

    return this->try_pop(v);
  }

  task_chunk* create_next_chunk(task_chunk* c) noexcept {
    // TODO: actually this method can throw exception because of new

    auto* next_created = c->next.load(std::memory_order::relaxed);
    if (next_created == nullptr) {
      // only one thread allowed to create next chunk
      bool expected_zone = false;
      while (!c->create_next_crit.compare_exchange_weak(expected_zone, true, std::memory_order::relaxed)) {
        expected_zone = false;
        std::this_thread::yield();
      }

      // this is crit zone only for current chunk but few threads can create different chunks at same time so _head_push and _free_chain need syncronization

      next_created = c->next.load(std::memory_order::relaxed);
      if (next_created) {
        c->create_next_crit.store(false, std::memory_order::relaxed);
        return next_created;
      }

      auto free = _free_chain.load(std::memory_order::relaxed);
      if (free == nullptr) {
        // create new one
        free = new task_chunk();
      } else {
        auto next = free->next.load(std::memory_order::relaxed);
        while (!_free_chain.compare_exchange_strong(free, next, std::memory_order::relaxed)) {
          ASYNC_CORO_ASSERT(free != nullptr);
          next = free->next.load(std::memory_order::relaxed);
        }

        free->next.store(nullptr, std::memory_order::relaxed);
        free->begin.store(0, std::memory_order::relaxed);
        free->end.store(0, std::memory_order::relaxed);
        free->num_used.store(0, std::memory_order::relaxed);
        free->free_protect.store(0, std::memory_order::relaxed);
      }

      c->next.store(free, std::memory_order::relaxed);

      task_chunk* expected = c;
      while (!_head_push.compare_exchange_weak(expected, free, std::memory_order::relaxed)) {
      }

      c->create_next_crit.store(false, std::memory_order::relaxed);
      return free;
    } else {
      return next_created;
    }
  }

 public:
  atomic_queue()
      : _head_pop(new task_chunk()), _free_chain(new task_chunk()) {
    _free_chain.load(std::memory_order::relaxed)->free_protect.store(-1, std::memory_order::relaxed);
    _head_push.store(_head_pop.load(std::memory_order::relaxed), std::memory_order::relaxed);
  }

  ~atomic_queue() noexcept {
    auto head1 = _head_pop.load(std::memory_order::relaxed);
    auto head2 = _free_chain.load(std::memory_order::relaxed);

    ASYNC_CORO_ASSERT(head1 != nullptr);
    ASYNC_CORO_ASSERT(head2 != nullptr);

    while (!_head_pop.compare_exchange_weak(head1, nullptr, std::memory_order::relaxed)) {
    }
    while (!_free_chain.compare_exchange_weak(head2, nullptr, std::memory_order::relaxed)) {
    }
    _head_push.store(nullptr, std::memory_order::relaxed);

    while (head1) {
      auto* next = head1->next.load(std::memory_order::relaxed);
      head1->next.store(nullptr, std::memory_order::relaxed);

      delete head1;

      head1 = next;
    }

    while (head2) {
      auto* next = head2->next.load(std::memory_order::relaxed);
      head2->next.store(nullptr, std::memory_order::relaxed);

      delete head2;

      head2 = next;
    }
  }

  template <typename... U>
  void push(U&&... v) noexcept(std::is_nothrow_constructible_v<T, U&&...>) {
    auto head = _head_push.load(std::memory_order::relaxed);
    ASYNC_CORO_ASSERT(head != nullptr);
    auto protect = head->free_protect.load(std::memory_order::relaxed);
    do {
      if (protect < 0) [[unlikely]] {
        // this chunk was freed try next
        head = _head_push.load(std::memory_order::relaxed);
        ASYNC_CORO_ASSERT(head != nullptr);
        protect = head->free_protect.load(std::memory_order::relaxed);
      }
    } while (!head->free_protect.compare_exchange_weak(protect, protect + 1, std::memory_order::relaxed));

    head->push(*this, std::forward<U>(v)...);
  }

  bool try_pop(T& v) noexcept(std::is_nothrow_move_assignable_v<T>) {
    auto head = _head_pop.load(std::memory_order::relaxed);
    ASYNC_CORO_ASSERT(head != nullptr);
    auto protect = head->free_protect.load(std::memory_order::relaxed);
    do {
      if (protect < 0) {
        // this chunk was freed try next
        head = _head_pop.load(std::memory_order::relaxed);
        ASYNC_CORO_ASSERT(head != nullptr);
        protect = head->free_protect.load(std::memory_order::relaxed);
      }
    } while (!head->free_protect.compare_exchange_weak(protect, protect + 1, std::memory_order::relaxed));

    // release of free_protect inside try_pop
    bool need_release = true;
    auto ans = head->try_pop(v, *this, need_release);
    if (need_release) [[likely]] {
      head->free_protect.fetch_sub(1, std::memory_order::relaxed);
    }
    return ans;
  }

  bool has_value() const noexcept {
    auto head = _head_pop.load(std::memory_order::relaxed);
    ASYNC_CORO_ASSERT(head != nullptr);
    return head->has_value();
  }

 private:
  std::atomic<task_chunk*> _head_pop;
  std::atomic<task_chunk*> _head_push;
  std::atomic<task_chunk*> _free_chain;
};

};  // namespace async_coro
