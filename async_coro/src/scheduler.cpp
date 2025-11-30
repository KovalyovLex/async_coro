#include <async_coro/base_handle.h>
#include <async_coro/callback.h>
#include <async_coro/config.h>
#include <async_coro/execution_system.h>
#include <async_coro/internal/scheduled_run_data.h>
#include <async_coro/scheduler.h>
#include <async_coro/thread_safety/unique_lock.h>

#include <algorithm>
#include <atomic>
#include <memory>
#include <thread>
#include <utility>

namespace async_coro {

scheduler::scheduler()
    : scheduler(std::make_unique<execution_system>(execution_system_config{})) {
}

scheduler::scheduler(i_execution_system::ptr system) noexcept
    : _execution_system(std::move(system)) {
  ASYNC_CORO_ASSERT(_execution_system);
}

scheduler::~scheduler() {
  unique_lock lock{_mutex};
  auto coros = std::move(_managed_coroutines);
  _is_destroying = true;
  lock.unlock();
  _execution_system = nullptr;

  for (auto* coro : coros) {
    if (coro != nullptr) {
      coro->on_task_freed_by_scheduler();
    }
  }
}

bool scheduler::is_current_thread_fits(execution_queue_mark execution_queue) noexcept {
  return _execution_system->is_current_thread_fits(execution_queue);
}

void scheduler::continue_execution_impl(base_handle& handle) {  // NOLINT(*complexity*)
  base_handle* handle_to_run = std::addressof(handle);

  while (handle_to_run != nullptr) {
    internal::scheduled_run_data run_data{};

    {
      internal::scheduled_run_data* curren_data{nullptr};

      while (!handle_to_run->_run_data.compare_exchange_strong(curren_data, std::addressof(run_data), std::memory_order::acquire, std::memory_order::relaxed)) {
        if (curren_data != nullptr && handle_to_run->is_current_thread_same()) {
          // push this coro to q on run next
          ASYNC_CORO_ASSERT(curren_data->coroutine_to_run_next == nullptr);
          curren_data->coroutine_to_run_next = handle_to_run;
          return;
        }
        curren_data = nullptr;
      }
    }

    bool was_cancelled = handle_to_run->set_coroutine_state_and_get_cancelled(coroutine_state::running);

    coroutine_state state = coroutine_state::created;
    if (!was_cancelled) {
      handle_to_run->get_handle().resume();

      std::tie(state, was_cancelled) = handle_to_run->get_coroutine_state_and_cancelled();
    } else {
      state = coroutine_state::suspended;
      handle_to_run->set_coroutine_state(state);
    }

    ASYNC_CORO_ASSERT(state != coroutine_state::running);

    if (was_cancelled || state == coroutine_state::finished) {
      const auto cancelled_without_finish = state != coroutine_state::finished && was_cancelled;

      if (auto* parent = handle_to_run->get_parent()) {
        if (parent->_current_child == handle_to_run) {
          parent->_current_child = nullptr;
        }

        if (cancelled_without_finish) {
          if (auto* on_cancel = handle_to_run->_on_cancel.exchange(nullptr, std::memory_order::relaxed)) {
            on_cancel->execute_and_destroy();
          }

          parent->request_cancel();
        }

        if (parent->get_coroutine_state() == coroutine_state::suspended) {
          // wake up parent coroutine
          if (parent->is_current_thread_same()) {
            ASYNC_CORO_ASSERT(run_data.coroutine_to_run_next == nullptr);
            run_data.coroutine_to_run_next = parent;
          } else {
            plan_continue_on_thread(*parent, parent->_execution_queue);
          }
        }
      } else {
        // cleanup coroutine

        if (cancelled_without_finish) {
          if (auto* on_cancel = handle_to_run->_on_cancel.exchange(nullptr, std::memory_order::relaxed)) {
            on_cancel->execute_and_destroy();
          }
        }

        cleanup_coroutine(*handle_to_run, cancelled_without_finish);
      }

    } else if (state == coroutine_state::waiting_switch) {
      change_execution_queue(*handle_to_run, handle_to_run->_execution_queue);
    }

    handle_to_run->_run_data.store(nullptr, std::memory_order::release);

    handle_to_run = run_data.coroutine_to_run_next;

    if (handle_to_run != nullptr && was_cancelled) {
      // cancel execution of next coroutine as it depends on currently cancelled
      handle_to_run->request_cancel();
      break;
    }
  }
}

void scheduler::cleanup_coroutine(base_handle& handle_impl, bool cancelled) {
  bool was_managed = false;
  {
    // remove from managed
    unique_lock lock{_mutex};
    auto iter = std::ranges::find(_managed_coroutines, &handle_impl);
    if (iter != _managed_coroutines.end()) {
      was_managed = true;
      if (*iter != _managed_coroutines.back()) {
        std::swap(*iter, _managed_coroutines.back());
      }
      _managed_coroutines.resize(_managed_coroutines.size() - 1);
    }
  }

#if ASYNC_CORO_WITH_EXCEPTIONS && ASYNC_CORO_COMPILE_WITH_EXCEPTIONS
  try {
    // try to handle exception by external api
    if (!handle_impl.execute_continuation(cancelled, false)) {
      handle_impl.check_exception_base();
    }
  } catch (...) {
    decltype(_exception_handler) handler_copy;
    {
      unique_lock lock{_mutex};
      handler_copy = _exception_handler;
    }
    if (handler_copy) {
      (*handler_copy)(std::current_exception());
    }
  }
#else
  handle_impl.execute_continuation(cancelled, false);
#endif

  if (was_managed) {
    handle_impl.on_task_freed_by_scheduler();
  }
}

void scheduler::plan_continue_on_thread(base_handle& handle_impl, execution_queue_mark execution_queue) {
  ASYNC_CORO_ASSERT(handle_impl._scheduler == this);

  _execution_system->plan_execution(
      [this, handle_base = &handle_impl, execution_queue]() {
        handle_base->_execution_thread = std::this_thread::get_id();
        handle_base->_execution_queue = execution_queue;
        this->continue_execution_impl(*handle_base);
      },
      execution_queue);
}

void scheduler::add_coroutine(base_handle& handle_impl,
                              callback_base::ptr start_function,
                              execution_queue_mark execution_queue) {
  ASYNC_CORO_ASSERT(handle_impl._execution_thread == std::thread::id{});
  ASYNC_CORO_ASSERT(handle_impl.get_coroutine_state() == coroutine_state::created);
  ASYNC_CORO_ASSERT(handle_impl.get_handle());

  handle_impl._start_function = std::move(start_function);

  {
    unique_lock lock{_mutex};

    if (_is_destroying) {
      lock.unlock();
      // if we are in destructor no way to run this coroutine
      handle_impl.on_task_freed_by_scheduler();
      return;
    }

    _managed_coroutines.push_back(&handle_impl);
  }

  handle_impl._scheduler = this;

  if (is_current_thread_fits(execution_queue)) {
    // start execution immediately if we in right thread

    handle_impl._execution_thread = std::this_thread::get_id();
    handle_impl._execution_queue = execution_queue;
    continue_execution_impl(handle_impl);
  } else {
    change_execution_queue(handle_impl, execution_queue);
  }
}

#if ASYNC_CORO_WITH_EXCEPTIONS && ASYNC_CORO_COMPILE_WITH_EXCEPTIONS
void scheduler::set_unhandled_exception_handler(unique_function<void(std::exception_ptr)> handler) noexcept {
  auto ptr = std::make_shared<unique_function<void(std::exception_ptr)>>(std::move(handler));

  unique_lock lock{_mutex};
  _exception_handler = std::move(ptr);
}
#endif

void scheduler::continue_execution(base_handle& handle_impl, passkey_any<internal::coroutine_suspender, base_handle> /*key*/) {
  ASYNC_CORO_ASSERT(handle_impl._execution_thread != std::thread::id{});
  ASYNC_CORO_ASSERT(handle_impl.get_coroutine_state() == coroutine_state::suspended);

  if (handle_impl.is_current_thread_same()) {
    // start execution immediately if we in right thread
    continue_execution_impl(handle_impl);
  } else {
    plan_continue_on_thread(handle_impl, handle_impl._execution_queue);
  }
}

void scheduler::change_execution_queue(base_handle& handle_impl,
                                       execution_queue_mark execution_queue) {
  ASYNC_CORO_ASSERT(!is_current_thread_fits(execution_queue));

  plan_continue_on_thread(handle_impl, execution_queue);
}

void scheduler::on_child_coro_added(base_handle& parent, base_handle& child, passkey<task_base> /*key*/) {
  ASYNC_CORO_ASSERT(parent.get_coroutine_state() == coroutine_state::running);
  ASYNC_CORO_ASSERT(parent._scheduler == this);
  ASYNC_CORO_ASSERT(child._execution_thread == std::thread::id{});
  ASYNC_CORO_ASSERT(child.get_coroutine_state() == coroutine_state::created);
  ASYNC_CORO_ASSERT(parent.is_current_thread_same());

  parent.set_coroutine_state(coroutine_state::suspended);

  child._scheduler = this;
  child._execution_thread = parent._execution_thread;
  child._execution_queue = parent._execution_queue;
  child.set_parent(parent);
  child.set_coroutine_state(coroutine_state::suspended);

  auto* run_data = parent._run_data.load(std::memory_order::relaxed);
  ASYNC_CORO_ASSERT(run_data != nullptr);
  ASYNC_CORO_ASSERT(run_data->coroutine_to_run_next == nullptr);

  run_data->coroutine_to_run_next = std::addressof(child);
}

}  // namespace async_coro
