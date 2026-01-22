#pragma once

#include <async_coro/config.h>
#include <async_coro/internal/callback_execute_command.h>
#include <async_coro/utils/callback_fwd.h>
#include <async_coro/utils/callback_ptr.h>
#include <async_coro/utils/get_owner.h>

#include <tuple>
#include <type_traits>
#include <utility>

namespace async_coro {

template <class TSelf, class TCallback>
  requires(std::is_base_of_v<callback_base<true>, TCallback> || std::is_base_of_v<callback_base<false>, TCallback>)
class callback_on_stack : public TCallback {
 protected:
  callback_on_stack() noexcept : TCallback(&execute) {}

  // on_destroy can be optionally overriden in TSelf
  void on_destroy() noexcept {}

  // on_execute_and_destroy(TArgs...) should be implemented in caller side

  template <class TOwner>
  TOwner& get_owner(TSelf TOwner::* member_ptr) noexcept {
    return async_coro::get_owner(static_cast<TSelf&>(*this), member_ptr);
  }

 public:
  callback_on_stack(const callback_on_stack&) = delete;
  callback_on_stack(callback_on_stack&&) noexcept = default;
  ~callback_on_stack() noexcept = default;

  callback_on_stack& operator=(const callback_on_stack&) = delete;
  callback_on_stack& operator=(callback_on_stack&&) = delete;

  callback_ptr<typename TCallback::execute_signature> get_ptr() noexcept {
    return callback_ptr<typename TCallback::execute_signature>(static_cast<TSelf*>(this));
  }

 private:
  static void execute(internal::callback_execute_command& cmd, callback_base<TCallback::is_noexcept>& clb) noexcept(TCallback::is_noexcept) {
    auto& self = static_cast<TSelf&>(clb);

    if (cmd.execute == internal::callback_execute_type::destroy) {
      self.on_destroy();
    } else {
      ASYNC_CORO_ASSERT(cmd.execute == internal::callback_execute_type::execute_and_destroy);

      auto& args = cmd.get_arguments<typename TCallback::execute_signature>();

      if constexpr (std::is_void_v<typename TCallback::return_type>) {
        std::apply(
            [&self](auto&&... t_args) noexcept(TCallback::is_noexcept) {
              return self.on_execute_and_destroy(std::forward<decltype(t_args)>(t_args)...);
            },
            std::move(args.args.get_value()));
      } else {
        args.set_result(std::apply(
            [&self](auto&&... t_args) noexcept(TCallback::is_noexcept) {
              return self.on_execute_and_destroy(std::forward<decltype(t_args)>(t_args)...);
            },
            std::move(args.args.get_value())));
      }
    }
  }
};

}  // namespace async_coro
