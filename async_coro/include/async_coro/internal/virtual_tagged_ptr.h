#pragma once

#include <async_coro/config.h>
#include <async_coro/internal/tagged_pair.h>

#include <atomic>
#include <cstddef>
#include <cstdint>

namespace async_coro::internal {

/**
 * @brief A high-bit tagged pointer for virtual address space tagging.
 *
 * This class wraps a pointer to type `T` and encodes a small integer tag in the *high bits* of the pointer,
 * leveraging unused bits in typical 64-bit virtual address spaces (e.g., only 48 bits are used on most systems).
 *
 * This is useful in lock-free and concurrent algorithms that benefit from compact metadata storage
 * alongside a pointer (e.g., generation counters, state flags). The pointer and tag are stored atomically
 * as a single integer value, allowing efficient and thread-safe operations.
 *
 * The number of high bits available for tagging is calculated as `sizeof(void*) * 8 - NumAddressBits`.
 *
 * @tparam T The pointed-to object type. Must be a complete type.
 * @tparam NumAddressBits Number of bits used to store actual pointer addresses.
 *         Defaults to 48, matching common virtual memory architectures.
 */
template <class T, std::uint32_t NumAddressBits = 48>
class virtual_tagged_ptr {
  static_assert(sizeof(void*) * 8 > NumAddressBits);

 public:
  /**
   * @brief Number of high bits available for tagging.
   */
  inline static constexpr std::uint32_t num_bits = sizeof(void*) * 8 - NumAddressBits;

  /**
   * @brief Maximum tag value that can be stored in available high bits.
   */
  inline static constexpr std::uint32_t max_tag_num = get_mask<std::uint32_t>(num_bits);

  /// Type representing a pair of pointer and tag.
  using tagged_pair = tagged_pair<T>;

  /**
   * @brief Constructs a tagged pointer from a raw pointer with a tag of 0.
   * @param ptr A pointer to a `T` instance.
   */
  virtual_tagged_ptr(T* ptr) noexcept
      : _raw_ptr(convert_tagged_to_raw({ptr, 0})) {
  }

  /**
   * @brief Constructs a null tagged pointer.
   */
  virtual_tagged_ptr(std::nullptr_t) noexcept
      : _raw_ptr(0) {
  }

  /**
   * @brief Default constructor initializes a null pointer.
   */
  virtual_tagged_ptr() noexcept = default;

  /**
   * @brief Atomically loads the current pointer and tag.
   *
   * @param order Memory ordering for the atomic load.
   * @return A `tagged_pair` containing the current pointer and tag.
   */
  tagged_pair load(std::memory_order order) const noexcept {
    const auto value = _raw_ptr.load(order);

    tagged_pair ans;
    convert_raw_to_tagged(ans, value);

    return ans;
  }

  /**
   * @brief Atomically stores a new tagged pointer.
   *
   * @param new_value The pointer and tag to store.
   * @param order Memory ordering for the atomic store.
   */
  void store(const tagged_pair& new_value, std::memory_order order) noexcept {
    _raw_ptr.store(convert_tagged_to_raw(new_value), order);
  }

  /**
   * @brief Atomically compares and exchanges the current tagged pointer.
   *
   * @param old_value On input, the expected current value; on failure, updated with the actual value.
   * @param new_value The new pointer and tag to store if the comparison succeeds.
   * @param order Memory ordering for the operation.
   * @return `true` if the exchange succeeded; `false` otherwise.
   */
  bool compare_exchange_strong(tagged_pair& old_value, const tagged_pair& new_value, std::memory_order order) noexcept {
    auto old_ptr = convert_tagged_to_raw(old_value);
    const auto new_ptr = convert_tagged_to_raw(new_value);

    const auto value = _raw_ptr.compare_exchange_strong(old_ptr, new_ptr, order);
    if (!value) {
      convert_raw_to_tagged(old_value, old_ptr);
    }

    return value;
  }

 private:
  /// Bitmask used to isolate the pointer portion of the stored integer.
  inline static constexpr std::uintptr_t address_mask = get_mask<std::uintptr_t>(NumAddressBits);

  /// Bitmask used to isolate the tag portion of the stored integer.
  inline static constexpr std::uintptr_t tag_mask = ~address_mask;

  static void convert_raw_to_tagged(tagged_pair& value, std::uintptr_t ptr_bits) noexcept {
    value.ptr = reinterpret_cast<T*>(ptr_bits & address_mask);
    value.tag = static_cast<std::uint32_t>((ptr_bits & tag_mask) >> NumAddressBits);
  }

  static std::uintptr_t convert_tagged_to_raw(const tagged_pair& value) noexcept {
    std::uintptr_t ptr_bits = reinterpret_cast<std::uintptr_t>(value.ptr);

    ASYNC_CORO_ASSERT((ptr_bits & tag_mask) == 0);

    ptr_bits |= (static_cast<std::uintptr_t>(value.tag) << NumAddressBits) & tag_mask;

    return ptr_bits;
  }

 private:
  /// The atomic raw integer holding the packed pointer and tag.
  std::atomic<std::uintptr_t> _raw_ptr;
};

}  // namespace async_coro::internal
