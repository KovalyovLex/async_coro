#include <async_coro/base_handle.h>
#include <async_coro/config.h>
#include <async_coro/execution_system.h>
#include <async_coro/scheduler.h>
#include <async_coro/thread_safety/unique_lock.h>

#include <algorithm>
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
  ASYNC_CORO_ASSERT(handle_impl.is_current_thread_same());

  bool was_coro_suspended = false;
  auto ptr_was_coro_suspended = handle_impl._was_coro_suspended ? handle_impl._was_coro_suspended : &was_coro_suspended;
  handle_impl.set_coroutine_state(coroutine_state::running);
  handle_impl._was_coro_suspended = ptr_was_coro_suspended;

  ASYNC_CORO_ASSERT(!*ptr_was_coro_suspended);

  handle_impl._handle.resume();

  if (*ptr_was_coro_suspended) {
    return false;
  }

  ASYNC_CORO_ASSERT(handle_impl._was_coro_suspended == nullptr || handle_impl._was_coro_suspended == ptr_was_coro_suspended);

  handle_impl._was_coro_suspended = nullptr;

  const auto state = handle_impl.get_coroutine_state();

  ASYNC_CORO_ASSERT(state != coroutine_state::running);

  if (state == coroutine_state::waiting_switch) {
    change_execution_queue(handle_impl, handle_impl._execution_queue);
  } else if (state == coroutine_state::finished) {
    if (auto* parent = handle_impl.get_parent(); continue_parent_on_finish && parent && parent->get_coroutine_state() == coroutine_state::suspended) {
      // wake up parent coroutine
      continue_execution(*handle_impl._parent, internal::passkey{this});
    } else if (!parent) {
      // cleanup coroutine
      *ptr_was_coro_suspended = true;

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
        if (!handle_impl.execute_continuation()) {
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
      handle_impl.execute_continuation();
#endif

      if (was_managed) {
        handle_impl.on_task_freed_by_scheduler();
      }
    }

    return true;
  }

  return false;
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
  ASYNC_CORO_ASSERT(handle_impl._handle);

  handle_impl._start_function = std::move(start_function);

  {
    unique_lock lock{_mutex};

    if (_is_destroying) {
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
