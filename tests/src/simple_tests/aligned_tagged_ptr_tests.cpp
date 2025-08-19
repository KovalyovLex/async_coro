#include <async_coro/internal/aligned_tagged_ptr.h>
#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <memory>

namespace tagged_ptrs {

template <class TValue>
class aligned_ptr : public ::testing::Test {
 public:
  using value_type = TValue;
};

class aligned_name_generator {
 public:
  template <typename T>
  static std::string GetName(int) {
    if constexpr (std::is_same_v<T, uint16_t>) return "uint16";
    if constexpr (std::is_same_v<T, int32_t>) return "int32";
    if constexpr (std::is_same_v<T, unsigned int>) return "unsignedInt";
    if constexpr (std::is_same_v<T, uint64_t>) return "uint64";
    if constexpr (std::is_same_v<T, float>) return "float";
    if constexpr (std::is_same_v<T, double>) return "double";
    if constexpr (std::is_same_v<T, long double>) return "longDouble";
  }
};

using test_aligned_types = ::testing::Types<uint16_t, int32_t, unsigned int, uint64_t, float, double, long double>;
TYPED_TEST_SUITE(aligned_ptr, test_aligned_types, aligned_name_generator);

struct malloc_deleter {
  template <class T>
  void operator()(T* ptr) noexcept {
    std::free(ptr);
  }
};

TEST(aligned_ptr, int_ptr_stack) {
  using value_type = int;

  using tagged_ptr = async_coro::internal::aligned_tagged_ptr<value_type, false>;

  tagged_ptr intptr;

  value_type val = value_type{0};

  EXPECT_EQ(alignof(value_type), 4);
  EXPECT_EQ(tagged_ptr::num_bits, 2);
  EXPECT_EQ(tagged_ptr::max_tag_num, 0b11);

  ASSERT_EQ(reinterpret_cast<intptr_t>(&val) & tagged_ptr::max_tag_num, 0);

  EXPECT_EQ(intptr.load(std::memory_order::relaxed).ptr, nullptr);
  EXPECT_EQ(intptr.load(std::memory_order::relaxed).tag, 0);

  tagged_ptr::tagged_ptr pair{nullptr, 0};
  ASSERT_TRUE(intptr.compare_exchange_strong(pair, {&val, 1}, std::memory_order::relaxed));

  EXPECT_EQ(intptr.load(std::memory_order::relaxed).ptr, &val);
  EXPECT_EQ(intptr.load(std::memory_order::relaxed).tag, 1);
}

TEST(aligned_ptr, int_ptr_heap) {
  using value_type = int;

  using tagged_ptr = async_coro::internal::aligned_tagged_ptr<value_type, true>;

  tagged_ptr intptr;

  auto val = std::unique_ptr<value_type, malloc_deleter>(new (std::malloc(sizeof(value_type))) value_type{0});

  EXPECT_EQ(alignof(value_type), 4);
  EXPECT_GE(tagged_ptr::num_bits, 3);

  ASSERT_EQ(reinterpret_cast<intptr_t>(val.get()) & tagged_ptr::max_tag_num, 0);

  EXPECT_EQ(intptr.load(std::memory_order::relaxed).ptr, nullptr);
  EXPECT_EQ(intptr.load(std::memory_order::relaxed).tag, 0);

  tagged_ptr::tagged_ptr pair{nullptr, 0};
  ASSERT_TRUE(intptr.compare_exchange_strong(pair, {val.get(), 1}, std::memory_order::relaxed));

  EXPECT_EQ(intptr.load(std::memory_order::relaxed).ptr, val.get());
  EXPECT_EQ(intptr.load(std::memory_order::relaxed).tag, 1);

  intptr.store({val.get(), 3}, std::memory_order::relaxed);

  EXPECT_EQ(intptr.load(std::memory_order::relaxed).ptr, val.get());
  EXPECT_EQ(intptr.load(std::memory_order::relaxed).tag, 3);
}

TYPED_TEST(aligned_ptr, ptr_stack) {
  using value_type = typename TestFixture::value_type;

  using tagged_ptr = async_coro::internal::aligned_tagged_ptr<value_type, false>;

  tagged_ptr intptr;

  value_type val = value_type{0};

  ASSERT_EQ(reinterpret_cast<intptr_t>(&val) & tagged_ptr::max_tag_num, 0);

  EXPECT_EQ(intptr.load(std::memory_order::relaxed).ptr, nullptr);
  EXPECT_EQ(intptr.load(std::memory_order::relaxed).tag, 0);

  typename tagged_ptr::tagged_ptr pair{nullptr, 0};
  ASSERT_TRUE(intptr.compare_exchange_strong(pair, {&val, 1}, std::memory_order::relaxed));

  EXPECT_EQ(intptr.load(std::memory_order::relaxed).ptr, &val);
  EXPECT_EQ(intptr.load(std::memory_order::relaxed).tag, 1);

  pair = {&val, 1};
  ASSERT_TRUE(intptr.compare_exchange_strong(pair, {nullptr, tagged_ptr::max_tag_num}, std::memory_order::relaxed));

  EXPECT_EQ(intptr.load(std::memory_order::relaxed).ptr, nullptr);
  EXPECT_EQ(intptr.load(std::memory_order::relaxed).tag, tagged_ptr::max_tag_num);
}

TYPED_TEST(aligned_ptr, ptr_heap) {
  using value_type = typename TestFixture::value_type;

  using tagged_ptr = async_coro::internal::aligned_tagged_ptr<value_type, true>;

  tagged_ptr intptr;

  auto val = std::unique_ptr<value_type, malloc_deleter>(new (std::malloc(sizeof(value_type))) value_type{0});

  ASSERT_EQ(reinterpret_cast<intptr_t>(val.get()) & tagged_ptr::max_tag_num, 0);

  EXPECT_EQ(intptr.load(std::memory_order::relaxed).ptr, nullptr);
  EXPECT_EQ(intptr.load(std::memory_order::relaxed).tag, 0);

  typename tagged_ptr::tagged_ptr pair{nullptr, 0};
  ASSERT_TRUE(intptr.compare_exchange_strong(pair, {val.get(), 1}, std::memory_order::relaxed));

  EXPECT_EQ(intptr.load(std::memory_order::relaxed).ptr, val.get());
  EXPECT_EQ(intptr.load(std::memory_order::relaxed).tag, 1);

  pair = {val.get(), 1};
  ASSERT_TRUE(intptr.compare_exchange_strong(pair, {nullptr, 3}, std::memory_order::relaxed));

  EXPECT_EQ(intptr.load(std::memory_order::relaxed).ptr, nullptr);
  EXPECT_EQ(intptr.load(std::memory_order::relaxed).tag, 3);
}

}  // namespace tagged_ptrs
