#pragma once

#include <async_coro/config.h>
#include <async_coro/internal/tagged_pair.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace async_coro::internal {

/**
 * @brief A lock-free tagged pointer with alignment-based tag encoding.
 *
 * This class implements a pointer wrapper that uses the low bits of an aligned pointer
 * to store a small integer "tag". It is useful in concurrent algorithms, particularly for
 * lock-free structures like hazard pointers or tagged memory reclamation schemes.
 *
 * The number of bits available for tagging is determined by the alignment of the type `T`.
 * If `OnlyMallocAllocated` is set to true (default), a conservative alignment is used to
 * ensure safe tagging for heap-allocated objects with malloc (or without support aligned new).
 *
 * Internally, the pointer and tag are packed into a single atomic integer for efficient
 * concurrent access and manipulation. The class provides lock-free `load`, `store`,
 * and `compare_exchange_strong` operations.
 *
 * @tparam T The pointed-to object type. Must be a complete type.
 * @tparam OnlyMallocAllocated If true, assumes the pointer is always heap-allocated,
 *         allowing use of stricter alignment for maximizing available tag bits.
 */
template <class T, bool OnlyMallocAllocated = true>
class aligned_tagged_ptr {
 public:
  /**
   * @brief Number of low bits safely usable for tagging based on pointer alignment.
   */
  inline static constexpr std::uint32_t num_bits = get_num_bits<std::uint32_t>(OnlyMallocAllocated ? std::max(alignof(T), alignof(std::max_align_t)) : alignof(T));

  /**
   * @brief Maximum numeric tag value that can be stored in available bits.
   */
  inline static constexpr std::uint32_t max_tag_num = get_mask<std::uint32_t>(num_bits);

  /// Type representing a pair of a pointer and its associated tag.
  using tagged_ptr = tagged_pair<T>;

  /**
   * @brief Constructs an aligned tagged pointer from a raw pointer with a tag of 0.
   *
   * @param ptr A pointer to a `T` instance.
   */
  aligned_tagged_ptr(T* ptr) noexcept
      : _raw_ptr(convert_tagged_to_raw({ptr, 0})) {
  }

  /**
   * @brief Constructs a null tagged pointer.
   */
  aligned_tagged_ptr(std::nullptr_t) noexcept
      : _raw_ptr(0) {
  }

  /**
   * @brief Default constructor initializes a null pointer.
   */
  aligned_tagged_ptr() noexcept = default;

  /**
   * @brief Atomically loads the current tagged pointer.
   *
   * @param order The memory ordering for the atomic load.
   * @return The current pointer and tag as a `tagged_ptr`.
   */
  tagged_ptr load(std::memory_order order) const noexcept {
    const auto value = _raw_ptr.load(order);

    tagged_ptr ans;
    convert_raw_to_tagged(ans, value);

    return ans;
  }

  /**
   * @brief Atomically stores a new tagged pointer.
   *
   * @param new_value The pointer and tag to store.
   * @param order The memory ordering for the atomic store.
   */
  void store(const tagged_ptr& new_value, std::memory_order order) noexcept {
    _raw_ptr.store(convert_tagged_to_raw(new_value), order);
  }

  /**
   * @brief Atomically compares and exchanges the current tagged pointer.
   *
   * @param old_value On input, the expected current value; on failure, updated with the actual current value.
   * @param new_value The new pointer and tag to store if comparison succeeds.
   * @param order The memory ordering for the operation.
   * @return `true` if the exchange succeeded, `false` otherwise.
   */
  bool compare_exchange_strong(tagged_ptr& old_value, const tagged_ptr& new_value, std::memory_order order) noexcept {
    auto old_ptr = convert_tagged_to_raw(old_value);
    const auto new_ptr = convert_tagged_to_raw(new_value);

    const auto value = _raw_ptr.compare_exchange_strong(old_ptr, new_ptr, order);
    if (!value) {
      convert_raw_to_tagged(old_value, old_ptr);
    }

    return value;
  }

 private:
  /// Mask for extracting the aligned pointer from a tagged integer.
  inline static constexpr std::uintptr_t address_mask = ~static_cast<std::uintptr_t>(max_tag_num);

  /// Mask for extracting the tag from a tagged integer.
  inline static constexpr std::uintptr_t tag_mask = max_tag_num;

  static void convert_raw_to_tagged(tagged_ptr& value, std::uintptr_t ptr_bits) noexcept {
    value.ptr = reinterpret_cast<T*>(ptr_bits & address_mask);
    value.tag = static_cast<std::uint32_t>(ptr_bits & tag_mask);
  }

  static std::uintptr_t convert_tagged_to_raw(const tagged_ptr& value) noexcept {
    std::uintptr_t ptr_bits = reinterpret_cast<std::uintptr_t>(value.ptr);

    ASYNC_CORO_ASSERT((ptr_bits & tag_mask) == 0);

    ptr_bits |= static_cast<std::uintptr_t>(value.tag) & tag_mask;

    return ptr_bits;
  }

 private:
  /// The atomic raw integer storing the tagged pointer.
  std::atomic<std::uintptr_t> _raw_ptr;
};

}  // namespace async_coro::internal
