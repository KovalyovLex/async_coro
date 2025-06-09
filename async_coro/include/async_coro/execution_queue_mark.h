#pragma once

#include <cstdint>

namespace async_coro {

class execution_thread_mask;

/// Class for marking different execution queues. Expected to work as enum starting with value 0 to max
class execution_queue_mark {
 public:
  explicit constexpr execution_queue_mark(std::uint8_t marker) noexcept : _marker(marker) {}
  execution_queue_mark(const execution_queue_mark &) noexcept = default;
  execution_queue_mark &operator=(const execution_queue_mark &) noexcept = default;

  constexpr std::uint8_t get_value() const noexcept {
    return _marker;
  }

  constexpr execution_thread_mask operator|(execution_queue_mark other) const noexcept;

  constexpr bool operator==(execution_queue_mark other) const noexcept {
    return _marker == other._marker;
  }

 private:
  std::uint8_t _marker;
};

/// Class for configuring allowed queues for execution thread
class execution_thread_mask {
  explicit constexpr execution_thread_mask(std::uint32_t mask) noexcept : _mask(mask) {}

 public:
  constexpr execution_thread_mask() noexcept : _mask(0) {}
  constexpr execution_thread_mask(execution_queue_mark marker) noexcept : _mask(1 << marker.get_value()) {}
  execution_thread_mask(const execution_thread_mask &) noexcept = default;
  execution_thread_mask &operator=(const execution_thread_mask &) noexcept = default;

  constexpr bool allowed(execution_thread_mask other) const noexcept {
    return (other._mask & _mask) != 0;
  }

  constexpr execution_thread_mask operator|(execution_thread_mask other) const noexcept {
    return execution_thread_mask{other._mask | _mask};
  }

  constexpr execution_thread_mask operator&(execution_thread_mask other) const noexcept {
    return execution_thread_mask{other._mask & _mask};
  }

 private:
  std::uint32_t _mask;
};

constexpr execution_thread_mask execution_queue_mark::operator|(execution_queue_mark other) const noexcept {
  return execution_thread_mask{other} | execution_thread_mask{*this};
}

namespace execution_queues {
inline constexpr execution_queue_mark main{0};
inline constexpr execution_queue_mark worker{1};
inline constexpr execution_queue_mark any{2};
}  // namespace execution_queues

}  // namespace async_coro