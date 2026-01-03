#pragma once

#include <async_coro/callback.h>
#include <async_coro/config.h>
#include <async_coro/execution_queue_mark.h>
#include <async_coro/internal/base_handle_ptr.h>
#include <async_coro/internal/coroutine_suspender.h>

#include <atomic>
#include <coroutine>
#include <cstdint>
#include <memory>
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
namespace internal {
class scheduled_run_data;
}

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
  friend internal::coroutine_suspender;
  friend base_handle_ptr;

  static constexpr uint8_t coroutine_state_mask = (1U << 0U) | (1U << 1U) | (1U << 2U);
  static constexpr uint8_t is_embedded_mask = (1U << 3U);
  static constexpr uint8_t is_cancel_requested_mask = (1U << 4U);
  static constexpr uint8_t is_initialized_mask = (1U << 5U);
  static constexpr uint8_t is_result_mask = (1U << 6U);

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
      : _parent(nullptr) {}

  base_handle(const base_handle&) = delete;
  base_handle(base_handle&&) = delete;

  base_handle& operator=(const base_handle&) = delete;
  base_handle& operator=(base_handle&&) = delete;

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
  [[nodiscard]] scheduler& get_scheduler() noexcept {
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
  [[nodiscard]] const scheduler& get_scheduler() const noexcept {
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
  [[nodiscard]] bool is_current_thread_same() const noexcept {
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
  [[nodiscard]] bool is_coro_embedded() const noexcept { return is_embedded(); }

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
  [[nodiscard]] bool is_finished_acquire() const noexcept { return get_coroutine_state(std::memory_order::acquire) == coroutine_state::finished; }

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
  [[nodiscard]] bool is_finished() const noexcept { return get_coroutine_state(std::memory_order::relaxed) == coroutine_state::finished; }

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
  [[nodiscard]] bool is_suspended() const noexcept { return get_coroutine_state() == coroutine_state::suspended; }

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
   * @brief Suspends coroutine and switches execution queue of coroutine
   *
   * This method sets the coroutine state to suspended state and updates the target execution queue.
   *
   * @param execution_queue The target execution queue for the coroutine
   * @param on_cancel Cancel callback
   *
   * @note This method is noexcept and will not throw exceptions
   * @note After this method continue_after_sleep should be called
   */
  void plan_sleep_on_queue(execution_queue_mark execution_queue, callback<void()>& on_cancel) noexcept {
    ASYNC_CORO_ASSERT_VARIABLE auto* prev = this->_on_cancel.exchange(std::addressof(on_cancel), std::memory_order::relaxed);
    ASYNC_CORO_ASSERT(prev == nullptr);

    set_coroutine_state(coroutine_state::suspended);
    _execution_queue = execution_queue;
  }

  /**
   * Continues execution of coroutine that previously was set on pause with plan_sleep_on_queue
   *
   * @note Should be called from thread that fits execution_queue
   */
  void continue_after_sleep();

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
  [[nodiscard]] execution_queue_mark get_execution_queue() const noexcept {
    return _execution_queue;
  }

  /**
   * @brief Returns object that manages current suspension
   *
   * @return `coroutine_suspender` object to call continue functions
   *
   * @note This method is noexcept and will not throw exceptions
   *
   * @see `coroutine_suspender`
   */
  auto suspend(std::uint32_t suspend_count, callback<void()>* on_cancel) noexcept {
    ASYNC_CORO_ASSERT_VARIABLE auto* prev = this->_on_cancel.exchange(on_cancel, std::memory_order::relaxed);
    ASYNC_CORO_ASSERT(prev == nullptr);

    return internal::coroutine_suspender{*this, suspend_count};
  }

  /**
   * @brief Requests coroutine to stop. Stop will happen on next suspension point of coroutine.
   *
   * @return previous state of cancel request
   */
  bool request_cancel();

  /**
   * @brief Returns state of cancel request
   *
   * @return previous state of cancel request
   */
  bool is_cancelled() noexcept {
    return (_atomic_state.load(std::memory_order::relaxed) & is_cancel_requested_mask) != 0;
  }

  /**
   * @brief Returns pair of coroutine_state and state of cancel request
   *
   * @return std::pair of coroutine_state and state of cancel request
   */
  [[nodiscard]] std::pair<coroutine_state, bool> get_coroutine_state_and_cancelled(std::memory_order order = std::memory_order::relaxed) const noexcept {
    const auto state = _atomic_state.load(order);
    return {static_cast<coroutine_state>(state & coroutine_state_mask), state & is_cancel_requested_mask};
  }

  base_handle_ptr get_owning_ptr();

 protected:
  // returns true if continuation was executed
  virtual bool execute_continuation(bool cancelled) = 0;

#if ASYNC_CORO_WITH_EXCEPTIONS
  // retrows exception if it was caught
  virtual void check_exception_base() = 0;
#endif

  [[nodiscard]] virtual std::coroutine_handle<> get_handle() noexcept = 0;

  void on_final_suspend() noexcept {
    set_coroutine_state(coroutine_state::finished, true);
  }

  void set_owning_by_task_handle(bool owning);

  void set_owning_by_task(bool owning);

  callback_base* release_continuation_functor() noexcept {
    return is_embedded() ? nullptr : _root_state.continuation.exchange(nullptr, std::memory_order::acquire);
  }

  void set_continuation_functor(callback_base* func) noexcept;

  [[nodiscard]] bool is_initialized() const noexcept {
    return (_atomic_state.load(std::memory_order::relaxed) & is_initialized_mask) != 0;
  }

  [[nodiscard]] bool is_result() const noexcept {
    return (_atomic_state.load(std::memory_order::relaxed) & is_result_mask) != 0;
  }

  void set_initialized(bool is_result) noexcept {
    update_value(is_initialized_mask | (is_result ? is_result_mask : 0U), get_inverted_mask(is_initialized_mask | is_result_mask));
  }

 private:
  static constexpr uint8_t get_inverted_mask(uint8_t mask) noexcept {
    return static_cast<uint8_t>(~mask);
  }

  void destroy_impl() noexcept;

  void dec_num_owners() noexcept;

  void inc_num_owners() noexcept;

  [[nodiscard]] base_handle* get_parent() const noexcept {
    return is_embedded() ? _parent : nullptr;
  }

  void set_parent(base_handle& parent) noexcept {
    _parent = &parent;
    parent._current_child = this->get_owning_ptr();
    set_embedded(true);
  }

  [[nodiscard]] coroutine_state get_coroutine_state(std::memory_order order = std::memory_order::relaxed) const noexcept {
    return static_cast<coroutine_state>(_atomic_state.load(order) & coroutine_state_mask);
  }

  void set_coroutine_state(coroutine_state value, bool release = false) noexcept {
    if (release) {
      update_value(static_cast<uint8_t>(value), get_inverted_mask(coroutine_state_mask), std::memory_order::relaxed, std::memory_order::release);
    } else {
      update_value(static_cast<uint8_t>(value), get_inverted_mask(coroutine_state_mask));
    }
  }

  // atomically updates state and returns previous value of cancel request
  bool set_coroutine_state_and_get_cancelled(coroutine_state value) noexcept {
    return (update_value(static_cast<uint8_t>(value), get_inverted_mask(coroutine_state_mask)) & is_cancel_requested_mask) != 0;
  }

  [[nodiscard]] bool is_embedded() const noexcept {
    return (_atomic_state.load(std::memory_order::relaxed) & is_embedded_mask) != 0;
  }

  void set_embedded(bool value) noexcept {
    update_value(value ? is_embedded_mask : 0U, get_inverted_mask(is_embedded_mask));
  }

  // returns previous value of cancel requested
  std::pair<bool, coroutine_state> set_cancel_requested() noexcept {
    const auto prev_state = update_value(is_cancel_requested_mask, get_inverted_mask(is_cancel_requested_mask));
    return {prev_state & is_cancel_requested_mask, static_cast<coroutine_state>(prev_state & coroutine_state_mask)};
  }

  uint8_t update_value(const uint8_t value, const uint8_t mask, std::memory_order read = std::memory_order::relaxed, std::memory_order write = std::memory_order::relaxed) noexcept {
    uint8_t expected = _atomic_state.load(read);
    while (!_atomic_state.compare_exchange_strong(expected, (expected & mask) | value, write, read)) {  // NOLINT(*-signed-bitwise)
    }
    return expected;
  }

 private:
  struct root_coro_state {
    std::atomic<callback_base*> continuation;
    callback_base::ptr start_function;  // we should store start function for all lifetime of the coroutine because it stores captured arguments
  };

  std::atomic<internal::scheduled_run_data*> _run_data{nullptr};
  union {
    root_coro_state _root_state;
    base_handle* _parent;
  };  // embedded coroutine cant have a continuation - continue can be assigned only for root coroutine
  scheduler* _scheduler = nullptr;
  std::atomic<callback<void()>*> _on_cancel = nullptr;  // callback for our coroutine. callback<void()> should be allocated on coroutine stack or outlive suspension point
  base_handle_ptr _current_child = nullptr;             // current awaiting child coroutine. Used for cancel notifications
  std::thread::id _execution_thread;
  execution_queue_mark _execution_queue = execution_queues::any;
  std::atomic_uint8_t _atomic_state{0};
  std::atomic_uint32_t _num_owners{0};
};

}  // namespace async_coro
