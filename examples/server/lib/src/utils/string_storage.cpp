#include <async_coro/config.h>
#include <server/utils/string_storage.h>

#include <cstring>
#include <memory>
#include <optional>
#include <string_view>

namespace server {

class dynamic_string_storage : public i_string_storage {
 public:
  explicit dynamic_string_storage(std::string str) : string(std::move(str)) {};

 protected:
  std::optional<std::string_view> try_put_string(std::string_view str) override {
    return std::nullopt;
  }
  void clear(i_string_storage::ptr &current_holder) noexcept override {
    current_holder = std::move(current_holder->next_storage);
  }

 public:
  std::string string;
};

i_string_storage::~i_string_storage() noexcept {
  // avoid recursive call of destructors
  auto ptr = std::move(next_storage);
  while (ptr) {
    ptr = std::move(ptr->next_storage);
  }
}

string_storage::string_storage() noexcept = default;  // NOLINT(*member-init*)

std::optional<std::string_view> string_storage::try_put_string(std::string_view str) {
  if (_empty_pos + str.size() <= kBufferSize) {
    auto *start_pos = &_buffer[_empty_pos];          // NOLINT(*bounds*)
    std::memcpy(start_pos, str.data(), str.size());  // NOLINT(*not-null-terminated*)
    std::string_view res{start_pos, str.size()};

    _empty_pos += str.size();
    return res;
  }

  return std::nullopt;
}

void string_storage::clear(ptr &this_holder) noexcept {
  ASYNC_CORO_ASSERT(this == this_holder.get());

  _empty_pos = 0;
  if (this->next_storage) {
    this->next_storage->clear(this->next_storage);
  }
}

void string_storage::clear(i_string_storage::ptr &current_holder) noexcept {
  _empty_pos = 0;
  if (this->next_storage) {
    this->next_storage->clear(this->next_storage);
  }
}

std::string_view string_storage::put_string(std::string_view str, std::string *str_to_move) {
  if (_empty_pos + str.size() <= kBufferSize) {
    auto *start_pos = &_buffer[_empty_pos];          // NOLINT(*bounds*)
    std::memcpy(start_pos, str.data(), str.size());  // NOLINT(*not-null-terminated*)
    std::string_view res{start_pos, str.size()};

    _empty_pos += str.size();
    return res;
  }

  if (!next_storage) {
    if (str.size() <= kBufferSize) {
      auto next = std::make_unique<string_storage>();
      auto res = next->put_string(str, str_to_move);
      next_storage.reset(next.release());
      return res;
    }

    std::string dyn_str = str_to_move != nullptr ? (std::move(*str_to_move)) : std::string{str};
    auto next = std::make_unique<dynamic_string_storage>(std::move(dyn_str));
    std::string_view res = next->string;
    next_storage = std::move(next);
    return res;
  }

  auto *next = next_storage.get();
  while (next != nullptr) {
    if (auto res = next_storage->try_put_string(str)) {
      return *res;
    }

    if (!next->next_storage) {
      if (str.size() <= kBufferSize) {
        auto next_store = std::make_unique<string_storage>();
        auto res = next_store->put_string(str, str_to_move);
        next->next_storage.reset(next_store.release());
        return res;
      }

      std::string dyn_str = str_to_move != nullptr ? (std::move(*str_to_move)) : std::string{str};
      auto next_store = std::make_unique<dynamic_string_storage>(std::move(dyn_str));
      std::string_view res = next_store->string;
      next->next_storage = std::move(next_store);
      return res;
    }

    next = next->next_storage.get();
  }

  return {};
}

}  // namespace server
