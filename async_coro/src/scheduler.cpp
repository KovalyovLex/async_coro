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

  for (auto& coro : coros) {
    if (coro) {
      coro->on_task_freed_by_scheduler();
    }
  }
}

bool scheduler::is_current_thread_fits(execution_queue_mark execution_queue) noexcept {
  return _execution_system->is_current_thread_fits(execution_queue);
}

bool scheduler::continue_execution_impl(base_handle& handle_impl, bool continue_parent_on_finish) {
  internal::scheduled_run_data local_run_data{};

  internal::scheduled_run_data* curren_data{nullptr};
  bool run_data_was_set = false;
  coroutine_state state;

  if (handle_impl._run_data.compare_exchange_strong(curren_data, &local_run_data, std::memory_order::relaxed)) {
    run_data_was_set = true;
    curren_data = &local_run_data;
  }

  bool was_cancelled = handle_impl.set_coroutine_state_and_get_cancelled(coroutine_state::running);

  ASYNC_CORO_ASSERT(!curren_data->external_continuation_request);

  if (!was_cancelled) {
    handle_impl.get_handle().resume();

    if (curren_data->external_continuation_request) {
      // this coroutine could be managed by another thread and it could be even destroyed so return immediately
      return false;
    }

    std::tie(state, was_cancelled) = handle_impl.get_coroutine_state_and_cancelled();
  } else {
    state = coroutine_state::suspended;
    handle_impl.set_coroutine_state(state);

    if (curren_data->external_continuation_request) {
      // this coroutine could be managed by another thread and it could be even destroyed so return immediately
      return false;
    }
  }

  if (run_data_was_set) {
    handle_impl._run_data.store(nullptr, std::memory_order::relaxed);
  }

  ASYNC_CORO_ASSERT(state != coroutine_state::running);

  if (was_cancelled || state == coroutine_state::finished) {
    const auto cancelled_without_finish = state != coroutine_state::finished && was_cancelled;

    auto* parent = handle_impl.get_parent();

    if (parent) {
      ASYNC_CORO_ASSERT(parent->_current_child == &handle_impl);

      parent->_current_child = nullptr;

      if (cancelled_without_finish) {
        if (auto* on_cancel = handle_impl._on_cancel.exchange(nullptr, std::memory_order::relaxed)) {
          on_cancel->execute_and_destroy();
        }

        parent->request_cancel();

        // current coroutine should not be processed further in recursive call
        curren_data->external_continuation_request = true;
      }

      if (continue_parent_on_finish && parent->get_coroutine_state() == coroutine_state::suspended) {
        // wake up parent coroutine
        continue_execution(*parent, internal::passkey{this});
      }
    } else {
      // cleanup coroutine
      curren_data->external_continuation_request = true;

      if (cancelled_without_finish) {
        if (auto* on_cancel = handle_impl._on_cancel.exchange(nullptr, std::memory_order::relaxed)) {
          on_cancel->execute_and_destroy();
        }
      }

      cleanup_coroutine(handle_impl, cancelled_without_finish);
    }
    return true;
  } else if (state == coroutine_state::waiting_switch) {
    change_execution_queue(handle_impl, handle_impl._execution_queue);
  }

  return false;
}

void scheduler::cleanup_coroutine(base_handle& handle_impl, bool cancelled) {
  bool was_managed = false;
  {
    // remove from managed
    unique_lock lock{_mutex};
    auto it = std::find(_managed_coroutines.begin(), _managed_coroutines.end(), &handle_impl);
    if (it != _managed_coroutines.end()) {
      was_managed = true;
      if (*it != _managed_coroutines.back()) {
        std::swap(*it, _managed_coroutines.back());
      }
      _managed_coroutines.resize(_managed_coroutines.size() - 1);
    }
  }

#if ASYNC_CORO_WITH_EXCEPTIONS && ASYNC_CORO_COMPILE_WITH_EXCEPTIONS
  try {
    // try to handle exception by external api
    if (!handle_impl.execute_continuation(cancelled)) {
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
  handle_impl.execute_continuation(cancelled);
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

void scheduler::continue_execution(base_handle& handle_impl, internal::passkey_any<coroutine_suspender, scheduler>) {
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

bool scheduler::on_child_coro_added(base_handle& parent, base_handle& child, internal::passkey<task_base>) {
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

  // start execution of internal coroutine
  bool was_done = continue_execution_impl(child, false);

  if (was_done) {
    parent.set_coroutine_state(coroutine_state::running);
  }

  return was_done;
}

}  // namespace async_coro
