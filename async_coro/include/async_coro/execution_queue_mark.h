#pragma once

#include <cstdint>

namespace async_coro {

class execution_thread_mask;

/**
 * @brief Class for marking different execution queues
 *
 * The execution_queue_mark class represents a specific execution queue identifier
 * in the async_coro execution system. It acts as a strongly-typed enum-like class
 * that provides type safety and prevents accidental mixing of queue identifiers
 * with other integer types.
 *
 * Execution queue marks are used to:
 * - Identify specific execution queues (main, worker, custom queues)
 * - Route tasks to appropriate execution contexts
 * - Configure thread permissions and capabilities
 * - Enable queue-specific task distribution strategies
 *
 * The class is designed to work as an enum starting with value 0 and incrementing
 * to a maximum value. It provides constexpr constructors and operators for
 * compile-time evaluation and efficient runtime performance.
 *
 * @note This class is designed for compile-time efficiency with constexpr operations
 * @note Queue mark values should be sequential starting from 0
 * @note The class provides type safety over raw integer queue identifiers
 */
class execution_queue_mark {
 public:
  /**
   * @brief Constructs an execution queue mark with the specified value
   *
   * Creates a new execution queue mark with the given numeric identifier.
   * The value should typically be sequential starting from 0.
   *
   * @param marker The numeric identifier for this execution queue (0-based)
   *
   * @note This constructor is constexpr for compile-time evaluation
   * @note The constructor is noexcept and will not throw exceptions
   */
  explicit constexpr execution_queue_mark(std::uint8_t marker) noexcept : _marker(marker) {}

  execution_queue_mark(const execution_queue_mark &) noexcept = default;
  execution_queue_mark &operator=(const execution_queue_mark &) noexcept = default;

  /**
   * @brief Returns the numeric value of this execution queue mark
   *
   * @return The underlying numeric identifier for this execution queue
   *
   * @note This method is constexpr for compile-time evaluation
   * @note This method is noexcept and will not throw exceptions
   */
  constexpr std::uint8_t get_value() const noexcept {
    return _marker;
  }

  /**
   * @brief Combines this queue mark with another to create a thread mask
   *
   * Performs a bitwise OR operation between this queue mark and another,
   * creating an execution_thread_mask that represents both queues.
   * This is useful for configuring threads to handle multiple queue types.
   *
   * @param other The other execution queue mark to combine with
   * @return An execution_thread_mask representing both queue marks
   *
   * @note This operator is constexpr for compile-time evaluation
   * @note This operator is noexcept and will not throw exceptions
   * @note The result can be used to configure thread permissions
   */
  constexpr execution_thread_mask operator|(execution_queue_mark other) const noexcept;

  /**
   * @brief Equality comparison operator
   *
   * Compares this execution queue mark with another for equality.
   *
   * @param other The execution queue mark to compare with
   * @return true if both queue marks have the same value, false otherwise
   *
   * @note This operator is constexpr for compile-time evaluation
   * @note This operator is noexcept and will not throw exceptions
   */
  constexpr bool operator==(execution_queue_mark other) const noexcept {
    return _marker == other._marker;
  }

 private:
  /** @brief The underlying numeric identifier for this execution queue */
  std::uint8_t _marker;
};

/**
 * @brief Class for configuring allowed queues for execution threads
 *
 * The execution_thread_mask class represents a bit mask that defines which
 * execution queues a thread is allowed to process. It provides a type-safe
 * way to configure thread permissions and capabilities in the execution system.
 *
 * Thread masks are used to:
 * - Define which execution queues a thread can process
 * - Control task distribution and load balancing
 * - Implement queue-specific execution strategies
 * - Enable fine-grained thread configuration
 *
 * The class provides bitwise operations (OR, AND) for combining multiple
 * queue permissions and checking if a thread is allowed to process specific queues.
 *
 * @note This class is designed for compile-time efficiency with constexpr operations
 * @note Thread masks use bitwise operations for efficient permission checking
 * @note The class provides type safety over raw integer bit masks
 */
class execution_thread_mask {
  /**
   * @brief Private constructor for creating thread masks from raw bit values
   *
   * @param mask The raw bit mask value
   * @note This constructor is constexpr for compile-time evaluation
   */
  explicit constexpr execution_thread_mask(std::uint32_t mask) noexcept : _mask(mask) {}

 public:
  /**
   * @brief Default constructor
   *
   * Creates an empty thread mask that doesn't allow any execution queues.
   * This represents a thread with no execution permissions.
   *
   * @note This constructor is constexpr for compile-time evaluation
   * @note This constructor is noexcept and will not throw exceptions
   */
  constexpr execution_thread_mask() noexcept : _mask(0) {}

  /**
   * @brief Constructs a thread mask from a single execution queue mark
   *
   * Creates a thread mask that allows processing of the specified execution queue.
   * The mask will have a single bit set corresponding to the queue mark.
   *
   * @param marker The execution queue mark to allow
   * @note This constructor is constexpr for compile-time evaluation
   * @note This constructor is noexcept and will not throw exceptions
   */
  constexpr execution_thread_mask(execution_queue_mark marker) noexcept : _mask(1 << marker.get_value()) {}

  execution_thread_mask(const execution_thread_mask &) noexcept = default;
  execution_thread_mask &operator=(const execution_thread_mask &) noexcept = default;

  /**
   * @brief Checks if this thread mask allows the specified queue permissions
   *
   * Performs a bitwise AND operation to determine if this thread mask
   * has any overlapping permissions with the specified mask.
   *
   * @param other The thread mask to check against
   * @return true if this thread mask allows any of the queues in other, false otherwise
   *
   * @note This method is constexpr for compile-time evaluation
   * @note This method is noexcept and will not throw exceptions
   * @note This method is commonly used to check if a thread can process a specific queue
   */
  constexpr bool allowed(execution_thread_mask other) const noexcept {
    return (other._mask & _mask) != 0;
  }

  /**
   * @brief Combines this thread mask with another using bitwise OR
   *
   * Creates a new thread mask that allows all queues from both this mask
   * and the specified mask. This is useful for granting additional permissions.
   *
   * @param other The thread mask to combine with
   * @return A new thread mask with combined permissions
   *
   * @note This operator is constexpr for compile-time evaluation
   * @note This operator is noexcept and will not throw exceptions
   * @note This operator is commonly used to grant multiple queue permissions
   */
  constexpr execution_thread_mask operator|(execution_thread_mask other) const noexcept {
    return execution_thread_mask{other._mask | _mask};
  }

  /**
   * @brief Intersects this thread mask with another using bitwise AND
   *
   * Creates a new thread mask that only allows queues that are present
   * in both this mask and the specified mask. This is useful for restricting permissions.
   *
   * @param other The thread mask to intersect with
   * @return A new thread mask with intersected permissions
   *
   * @note This operator is constexpr for compile-time evaluation
   * @note This operator is noexcept and will not throw exceptions
   * @note This operator is commonly used to restrict queue permissions
   */
  constexpr execution_thread_mask operator&(execution_thread_mask other) const noexcept {
    return execution_thread_mask{other._mask & _mask};
  }

 private:
  /** @brief The underlying bit mask representing allowed execution queues */
  std::uint32_t _mask;
};

/**
 * @brief Implementation of the bitwise OR operator for execution queue marks
 *
 * Combines two execution queue marks to create an execution thread mask
 * that represents both queues. This allows queue marks to be used directly
 * in thread mask operations.
 *
 * @param other The other execution queue mark to combine with
 * @return An execution_thread_mask representing both queue marks
 *
 * @note This operator is constexpr for compile-time evaluation
 * @note This operator is noexcept and will not throw exceptions
 */
constexpr execution_thread_mask execution_queue_mark::operator|(execution_queue_mark other) const noexcept {
  return execution_thread_mask{other} | execution_thread_mask{*this};
}

/**
 * @brief Namespace containing predefined execution queue constants
 *
 * This namespace provides standard execution queue marks that are commonly
 * used in the async_coro execution system. These constants define the basic
 * execution contexts available to applications.
 *
 * @note These constants are inline constexpr for compile-time efficiency
 * @note Queue values are sequential starting from 0 for optimal bit mask operations
 */
namespace execution_queues {

/**
 * @brief Execution queue for main thread tasks
 *
 * Represents the execution queue for tasks that should be executed on the main thread.
 * This queue is typically used for UI updates, input processing, and other
 * main thread-specific operations.
 *
 * @note Value: 0 (first queue mark)
 * @note This queue is typically processed by the main thread
 * @note Tasks in this queue should be lightweight to avoid blocking the main thread
 */
inline constexpr execution_queue_mark main{0};

/**
 * @brief Execution queue for worker thread tasks
 *
 * Represents the execution queue for tasks that should be executed on worker threads.
 * This queue is typically used for CPU-intensive operations, I/O operations,
 * and other tasks that can benefit from parallel execution.
 *
 * @note Value: 1 (second queue mark)
 * @note This queue is typically processed by worker threads in the thread pool
 * @note Tasks in this queue can be long-running without affecting the main thread
 */
inline constexpr execution_queue_mark worker{1};

/**
 * @brief Execution queue for tasks that can run on any thread
 *
 * Represents a flexible execution queue for tasks that can be executed on
 * any available thread (main or worker). This queue provides maximum flexibility
 * for task distribution and load balancing.
 *
 * @note Value: 2 (third queue mark)
 * @note This queue can be processed by any thread with appropriate permissions
 * @note Tasks in this queue are distributed based on thread availability and load
 */
inline constexpr execution_queue_mark any{2};

}  // namespace execution_queues

}  // namespace async_coro
