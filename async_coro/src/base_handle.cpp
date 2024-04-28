#include <async_coro/base_handle.h>
#include <async_coro/scheduler.h>

namespace async_coro {
void base_handle::on_child_coro_added(base_handle& child) {
  get_scheduler().on_child_coro_added(*this, child);
}
}  // namespace async_coro
