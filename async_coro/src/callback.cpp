#include <async_coro/callback.h>

namespace async_coro {

void callback_base::destroy() noexcept {
  if (_deleter != nullptr) {
    _deleter(this);
  }
}

void callback_base::default_deleter(callback_base* ptr) noexcept {
  delete ptr;  // NOLINT
}

}  // namespace async_coro
