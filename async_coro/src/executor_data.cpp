#include <async_coro/executor_data.h>

#include <algorithm>
#include <cstdint>
#include <mutex>
#include <thread>
#include <typeindex>
#include <unordered_map>

namespace async_coro::internal {

uint32_t get_key_for_type(std::type_index type) {
  struct key_storage {
    std::unordered_map<std::type_index, uint32_t> map;
    std::mutex mutex;
    uint32_t index = 1;
  };

  static key_storage storage{};

  auto& storage_ref = storage;

  std::unique_lock lock{storage_ref.mutex};

  auto& ref = storage_ref.map[type];
  if (ref == 0) {
    // new key
    ref = storage_ref.index++;
  }
  return ref;
}

}  // namespace async_coro::internal

namespace async_coro {

executor_data::executor_data() noexcept
    : _owning_thread(std::this_thread::get_id()) {
}

executor_data::executor_data(executor_data&&) noexcept = default;
executor_data::~executor_data() noexcept = default;
executor_data& executor_data::operator=(executor_data&&) noexcept = default;

void* executor_data::find_data(data_key key, creator_func_t creator) const {
  constexpr size_t use_bin_search_after = 32;

  void* result = nullptr;

  if (_data.size() > use_bin_search_after) {
    auto pos = std::lower_bound(_data.begin(), _data.end(), key, [](const store& store, data_key search) noexcept {  // NOLINT(*ranges*) ranges cant use different types in comp and value
      return store.key < search;
    });

    if (pos == _data.end() || pos->key != key) {
      pos = _data.emplace(pos, store{.key = key, .data = creator()});
    }
    result = pos->data.get();
  } else {
    const auto pos = std::ranges::find_if(_data, [key](const store& store) noexcept {
      return store.key == key;
    });

    if (pos == _data.end()) {
      _data.emplace_back(store{.key = key, .data = creator()});
      result = _data.back().data.get();
      if (_data.size() > use_bin_search_after) {
        std::ranges::sort(_data, [](const store& left, const store& right) noexcept { return left.key < right.key; });
      }
    } else {
      result = pos->data.get();
    }
  }

  return result;
}

}  // namespace async_coro
