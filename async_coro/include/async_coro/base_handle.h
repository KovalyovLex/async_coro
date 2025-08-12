#pragma once

#include <async_coro/callback.h>
#include <async_coro/config.h>
#include <async_coro/execution_queue_mark.h>

#include <atomic>
#include <coroutine>
#include <cstdint>
#include <thread>

namespace async_coro {

/**
 * @brief Enumeration representing the different states of a coroutine
 *
 * This enum defines the various states that a coroutine can be in during its lifecycle.
 * The states are used by the base_handle class to track and manage coroutine execution.
 */
enum class coroutine_state : std::uint8_t {
  /** @brief Coroutine has been created but not yet started */
  created = 0,
  /** @brief Coroutine is currently executing */
  running,
  /** @brief Coroutine is suspended waiting for resumption */
  suspended,
  /** @brief Coroutine is waiting to switch to a different execution queue */
  waiting_switch,
  /** @brief Coroutine has finished execution */
  finished
};

class scheduler;

/**
 * @brief Base class for coroutine handle management
 *
 * The base_handle class provides the fundamental infrastructure for managing
 * coroutine lifecycle, state tracking, and execution context. It serves as the
 * foundation for all coroutine-based asynchronous operations in the async_coro library.
 *
 * Key responsibilities:
 * - Coroutine state management and tracking
 * - Execution queue management and switching
 * - Thread affinity tracking
 * - Continuation and callback management
 * - Ownership and reference counting
 * - Integration with the scheduler system
 *
 * The class uses atomic operations for thread-safe state management and provides
 * a unified interface for coroutine control flow. It supports both embedded
 * and standalone coroutine handles with different ownership semantics.
 *
 * @note This class is designed for internal use by the async_coro library
 * @note All public methods are thread-safe unless otherwise specified
 * @note The class uses atomic operations for state management
 */
class base_handle {
  friend scheduler;

 public:
  /**
   * @brief Default constructor
   *
   * Creates a new base_handle instance with default initialization.
   * The handle starts in an uninitialized state and must be properly
   * initialized before use.
   *
   * @note This constructor is noexcept and will not throw exceptions
   * @note The handle must be initialized with a coroutine before use
   */
  base_handle() noexcept
      : _parent(nullptr),
        _is_initialized(false),
        _is_result(false) {}

  base_handle(const base_handle&) = delete;
  base_handle(base_handle&&) = delete;

  /**
   * @brief Destructor
   *
   * Properly cleans up the base_handle and its associated resources.
   * Ensures proper destruction of the coroutine and continuation objects.
   *
   * @note This destructor is noexcept and will not throw exceptions
   */
  virtual ~base_handle() noexcept;

  /**
   * @brief Returns a reference to the associated scheduler
   *
   * Provides access to the scheduler instance that manages this coroutine handle.
   * The scheduler is responsible for executing and managing the coroutine's lifecycle.
   *
   * @return Reference to the scheduler instance
   *
   * @note This method is noexcept and will not throw exceptions
   * @note The scheduler must be properly initialized before calling this method
   * @note This method will assert if the scheduler is null
   */
  scheduler& get_scheduler() noexcept {
    ASYNC_CORO_ASSERT(_scheduler != nullptr);
    return *_scheduler;
  }

  /**
   * @brief Returns a const reference to the associated scheduler
   *
   * Provides read-only access to the scheduler instance that manages this coroutine handle.
   *
   * @return Const reference to the scheduler instance
   *
   * @note This method is noexcept and will not throw exceptions
   * @note The scheduler must be properly initialized before calling this method
   * @note This method will assert if the scheduler is null
   */
  const scheduler& get_scheduler() const noexcept {
    ASYNC_CORO_ASSERT(_scheduler != nullptr);
    return *_scheduler;
  }

  /**
   * @brief Checks if the current thread is the same as the execution thread
   *
   * Determines whether the calling thread is the same thread that was
   * originally assigned to execute this coroutine.
   *
   * @return true if the current thread is the execution thread, false otherwise
   *
   * @note This method is noexcept and will not throw exceptions
   * @note This method is useful for determining if immediate execution is safe
   * @note The result may change if the coroutine switches execution contexts
   */
  bool is_current_thread_same() const noexcept {
    return _execution_thread == std::this_thread::get_id();
  }

  /**
   * @brief Checks if this coroutine handle is embedded in another handle
   *
   * Determines whether this base_handle is embedded within another coroutine handle.
   * Embedded handles have different ownership and lifecycle semantics compared to
   * standalone handles.
   *
   * @return true if this handle is embedded, false otherwise
   *
   * @note This method is noexcept and will not throw exceptions
   * @note Embedded handles are managed by their parent handle
   * @note This is an alias for is_embedded() for public API consistency
   */
  bool is_coro_embedded() const noexcept { return is_embedded(); }

  /**
   * @brief Checks if the coroutine has finished using acquire memory ordering
   *
   * Determines whether the coroutine has completed execution using acquire
   * memory ordering for thread synchronization.
   *
   * @return true if the coroutine state is finished, false otherwise
   *
   * @note This method is noexcept and will not throw exceptions
   * @note Uses acquire memory ordering for proper thread synchronization
   * @note This method provides stronger memory ordering guarantees than is_finished()
   */
  bool is_finished_acquire() const noexcept { return get_coroutine_state(std::memory_order::acquire) == coroutine_state::finished; }

  /**
   * @brief Checks if the coroutine has finished using relaxed memory ordering
   *
   * Determines whether the coroutine has completed execution using relaxed
   * memory ordering for maximum performance.
   *
   * @return true if the coroutine state is finished, false otherwise
   *
   * @note This method is noexcept and will not throw exceptions
   * @note Uses relaxed memory ordering for maximum performance
   * @note This method provides weaker memory ordering guarantees than is_finished_acquire()
   */
  bool is_finished() const noexcept { return get_coroutine_state(std::memory_order::relaxed) == coroutine_state::finished; }

  /**
   * @brief Checks if the coroutine is currently suspended
   *
   * Determines whether the coroutine is in a suspended state, waiting for
   * resumption or completion of an asynchronous operation.
   *
   * @return true if the coroutine state is suspended, false otherwise
   *
   * @note This method is noexcept and will not throw exceptions
   * @note Uses relaxed memory ordering for performance
   * @note Suspended coroutines are waiting for some condition to be met
   */
  bool is_suspended() const noexcept { return get_coroutine_state() == coroutine_state::suspended; }

  /**
   * @brief Marks the coroutine as suspended
   *
   * Should be called on every await_suspend in child coroutines to properly
   * track the coroutine state. This method updates the internal state to
   * indicate that the coroutine is waiting for resumption.
   *
   * @note This method is noexcept and will not throw exceptions
   * @note This method should be called from await_suspend implementations
   * @note The state change is atomic and thread-safe
   */
  void on_suspended() noexcept {
    set_coroutine_state(coroutine_state::suspended);
  }

  /**
   * @brief Switches the coroutine to a different execution queue
   *
   * Should be called instead of on_suspended when the coroutine needs to
   * switch to a different execution queue. This method sets the coroutine
   * state to waiting_switch and updates the target execution queue.
   *
   * @param execution_queue The target execution queue for the coroutine
   *
   * @note This method is noexcept and will not throw exceptions
   * @note This method should be called when switching execution contexts
   * @note The state change and queue update are atomic and thread-safe
   */
  void switch_execution_queue(execution_queue_mark execution_queue) noexcept {
    set_coroutine_state(coroutine_state::waiting_switch);
    _execution_queue = execution_queue;
  }

  /**
   * @brief Returns the current execution queue for this coroutine
   *
   * Provides access to the execution queue that this coroutine is currently
   * assigned to or will be executed on.
   *
   * @return The current execution queue mark
   *
   * @note This method is noexcept and will not throw exceptions
   * @note The execution queue may change during the coroutine's lifetime
   * @note This method uses relaxed memory ordering for performance
   */
  execution_queue_mark get_execution_queue() const noexcept {
    return _execution_queue;
  }

 protected:
  virtual void execute_continuation() = 0;

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
