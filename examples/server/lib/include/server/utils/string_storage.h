#pragma once

#include <array>
#include <memory>
#include <optional>
#include <string_view>

namespace server {

class i_string_storage {
 public:
  using ptr = std::unique_ptr<i_string_storage>;

  i_string_storage() noexcept = default;
  i_string_storage(const i_string_storage&) = delete;
  i_string_storage(i_string_storage&&) = delete;

  i_string_storage& operator=(const i_string_storage&) = delete;
  i_string_storage& operator=(i_string_storage&&) = delete;

  virtual std::optional<std::string_view> try_put_string(std::string_view str) = 0;

  virtual void clear(ptr& current_holder) noexcept = 0;

  virtual ~i_string_storage() noexcept;

  ptr next_storage;
};

class string_storage : i_string_storage {
 public:
  using ptr = std::unique_ptr<string_storage>;

  string_storage() noexcept;

  std::string_view put_string(std::string_view str, std::string* str_to_move);

  void clear(ptr& this_holder) noexcept;

 protected:
  std::optional<std::string_view> try_put_string(std::string_view str) override;
  void clear(i_string_storage::ptr& current_holder) noexcept override;

 private:
  static constexpr size_t kBufferSize = 1024;

  std::array<char, kBufferSize> _buffer;
  size_t _empty_pos = 0;
};

}  // namespace server
