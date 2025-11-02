#include <async_coro/utils/function_view.h>
#include <gtest/gtest.h>

using namespace async_coro;

int free_int() { return 42; }

void free_set_int(int& v) { v = 7; }

TEST(function_view, default_and_nullptr_compare) {
  function_view<void()> f;

  EXPECT_FALSE(f);
  EXPECT_TRUE(f == nullptr);
  EXPECT_TRUE(nullptr == f);
  EXPECT_FALSE(f != nullptr);
  EXPECT_FALSE(nullptr != f);
}

TEST(function_view, call_from_function_pointer_and_swap) {
  // explicit non-const view constructed from function pointer
  function_view<int()> f1(&free_int);
  EXPECT_TRUE(f1);
  EXPECT_EQ(f1(), 42);

  function_view<int()> f2(nullptr);
  std::swap(f1, f2);

  EXPECT_FALSE(f1);
  EXPECT_TRUE(f2);
  EXPECT_EQ(f2(), 42);

  f2.clear();
  EXPECT_FALSE(f2);
}

TEST(function_view, lambda_capture_and_noexcept_check) {
  struct test {
    static void call_noexcept(function_view<void() noexcept> fn) noexcept {
      EXPECT_TRUE(noexcept(fn()));
      ASSERT_TRUE(fn);
      fn();
    }

    static void call_non_noexcept(function_view<void()> fn) noexcept {
      EXPECT_FALSE(noexcept(fn()));
      ASSERT_TRUE(fn);
      fn();
    }
  };

  bool called = false;

  // noexcept view
  EXPECT_FALSE(called);
  test::call_noexcept([&called]() noexcept { called = true; });
  EXPECT_TRUE(called);

  // non-noexcept view
  bool called2 = false;
  EXPECT_FALSE(called2);
  test::call_non_noexcept([&called2]() { called2 = true; });
  EXPECT_TRUE(called2);
}

TEST(function_view, lref_and_rref_args) {
  int val = 0;

  function_view<void(int&)> f_lref(&free_set_int);
  EXPECT_FALSE(noexcept(f_lref(val)));
  f_lref(val);
  EXPECT_EQ(val, 7);

  // rvalue reference: passing temporary should call with moved-from object address
  static int global = 0;
  function_view<void(int&&)> f_rref([](auto&& i) noexcept { ASSERT_EQ(&global, &i); });
  f_rref(std::move(global));
}

TEST(function_view, deduction_and_const_vs_mutable_checks) {
  // construct different specializations explicitly and check noexcept traits
  auto lm = []() {};
  function_view<void()> vc(lm);
  EXPECT_FALSE(noexcept(vc()));

  function_view<void() noexcept> vn([]() noexcept {});
  EXPECT_TRUE(noexcept(vn()));
}
