#include <async_coro/callback.h>

namespace async_coro {

void callback_base::destroy() noexcept {
  if (_deleter) {
    _deleter(this);
  } else {
    delete this;
  }
}

void callback_base::stack_deleter(callback_base*) noexcept {
  // do nothing
}

}  // namespace async_coro
