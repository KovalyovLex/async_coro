#include <async_coro/internal/virtual_tagged_ptr.h>
#include <gtest/gtest.h>

#include <atomic>
#include <cstdint>
#include <memory>

namespace tagged_ptrs {

template <class TValue>
class virtual_ptr_test : public ::testing::Test {
 public:
  using value_type = TValue;
};

class virtual_name_generator {
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

using test_virtual_types = ::testing::Types<uint16_t, int32_t, unsigned int, uint64_t, float, double, long double>;
TYPED_TEST_SUITE(virtual_ptr_test, test_virtual_types, virtual_name_generator);

TEST(virtual_ptr_test, int_ptr_stack) {
  using value_type = int;

  using tagged_ptr = async_coro::internal::virtual_tagged_ptr<value_type>;

  tagged_ptr intptr;

  value_type val = value_type{0};

  EXPECT_EQ(tagged_ptr::num_bits, 16);
  EXPECT_GT(tagged_ptr::max_tag_num, 1 << 8);

  EXPECT_EQ(intptr.load(std::memory_order::relaxed).ptr, nullptr);
  EXPECT_EQ(intptr.load(std::memory_order::relaxed).tag, 0);

  tagged_ptr::tagged_ptr pair{nullptr, 0};
  ASSERT_TRUE(intptr.compare_exchange_strong(pair, {&val, 1}, std::memory_order::relaxed));

  EXPECT_EQ(intptr.load(std::memory_order::relaxed).ptr, &val);
  EXPECT_EQ(intptr.load(std::memory_order::relaxed).tag, 1);

  pair = {&val, 1};
  ASSERT_TRUE(intptr.compare_exchange_strong(pair, {nullptr, 513}, std::memory_order::relaxed));

  EXPECT_EQ(intptr.load(std::memory_order::relaxed).ptr, nullptr);
  EXPECT_EQ(intptr.load(std::memory_order::relaxed).tag, 513);

  pair = {nullptr, 513};
  ASSERT_TRUE(intptr.compare_exchange_strong(pair, {&val, tagged_ptr::max_tag_num}, std::memory_order::relaxed));

  EXPECT_EQ(intptr.load(std::memory_order::relaxed).ptr, &val);
  EXPECT_EQ(intptr.load(std::memory_order::relaxed).tag, tagged_ptr::max_tag_num);
}

TYPED_TEST(virtual_ptr_test, ptr_stack) {
  using value_type = typename TestFixture::value_type;

  using tagged_ptr = async_coro::internal::virtual_tagged_ptr<value_type>;

  tagged_ptr intptr;

  value_type val = value_type{0};
  value_type val2 = value_type{5};

  EXPECT_EQ(intptr.load(std::memory_order::relaxed).ptr, nullptr);
  EXPECT_EQ(intptr.load(std::memory_order::relaxed).tag, 0);

  typename tagged_ptr::tagged_ptr pair{nullptr, 0};
  ASSERT_TRUE(intptr.compare_exchange_strong(pair, {&val, 1}, std::memory_order::relaxed));

  EXPECT_EQ(intptr.load(std::memory_order::relaxed).ptr, &val);
  EXPECT_EQ(intptr.load(std::memory_order::relaxed).tag, 1);

  pair = {&val, 1};
  ASSERT_TRUE(intptr.compare_exchange_strong(pair, {&val2, 3}, std::memory_order::relaxed));

  EXPECT_EQ(intptr.load(std::memory_order::relaxed).ptr, &val2);
  EXPECT_EQ(intptr.load(std::memory_order::relaxed).tag, 3);

  pair = {&val2, 3};
  ASSERT_TRUE(intptr.compare_exchange_strong(pair, {&val2, 255}, std::memory_order::relaxed));

  EXPECT_EQ(intptr.load(std::memory_order::relaxed).ptr, &val2);
  EXPECT_EQ(intptr.load(std::memory_order::relaxed).tag, 255);

  pair = {&val2, 255};
  ASSERT_TRUE(intptr.compare_exchange_strong(pair, {&val, tagged_ptr::max_tag_num}, std::memory_order::relaxed));

  EXPECT_EQ(intptr.load(std::memory_order::relaxed).ptr, &val);
  EXPECT_EQ(intptr.load(std::memory_order::relaxed).tag, tagged_ptr::max_tag_num);
}

TYPED_TEST(virtual_ptr_test, ptr_heap) {
  using value_type = typename TestFixture::value_type;

  using tagged_ptr = async_coro::internal::virtual_tagged_ptr<value_type>;

  tagged_ptr intptr;

  auto val = std::make_unique<value_type>(value_type{0});
  auto val2 = value_type{2};

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

  pair = {nullptr, 3};
  ASSERT_TRUE(intptr.compare_exchange_strong(pair, {&val2, 255}, std::memory_order::relaxed));

  EXPECT_EQ(intptr.load(std::memory_order::relaxed).ptr, &val2);
  EXPECT_EQ(intptr.load(std::memory_order::relaxed).tag, 255);

  pair = {&val2, 255};
  ASSERT_TRUE(intptr.compare_exchange_strong(pair, {val.get(), tagged_ptr::max_tag_num}, std::memory_order::relaxed));

  EXPECT_EQ(intptr.load(std::memory_order::relaxed).ptr, val.get());
  EXPECT_EQ(intptr.load(std::memory_order::relaxed).tag, tagged_ptr::max_tag_num);
}

}  // namespace tagged_ptrs
