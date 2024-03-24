#include <async_coro/move_only_function.h>
#include <gtest/gtest.h>

#include <chrono>
#include <iostream>
#include <stdexcept>

#include "memory_hooks.h"

template <bool is_noexcept>
static auto test_small_f() {
  using namespace async_coro;

  const size_t before = mem_hook::num_allocated;

  move_only_function<void() noexcept(is_noexcept)> f;

  EXPECT_FALSE(f);
  static bool was_called = false;
  was_called = false;

  f = []() noexcept(is_noexcept) {
    was_called = true;

    if constexpr (!is_noexcept) {
      throw std::runtime_error("Test error");
    }
  };

  EXPECT_EQ(before, mem_hook::num_allocated);

  ASSERT_TRUE(f);

  auto f2 = std::move(f);

  EXPECT_FALSE(f);
  ASSERT_TRUE(f2);

  if constexpr (is_noexcept) {
    f2();
  } else {
    try {
      f2();
    } catch (std::runtime_error& e) {
      EXPECT_STREQ(e.what(), "Test error");
    }
  }

  EXPECT_TRUE(was_called);

  f = nullptr;
  EXPECT_FALSE(f);

  f2 = nullptr;
  EXPECT_FALSE(f);

  EXPECT_EQ(before, mem_hook::num_allocated);
}

TEST(move_only_function, except_small_f) { test_small_f<false>(); }

TEST(move_only_function, noexcept_small_f) { test_small_f<true>(); }

template <bool is_noexcept>
static auto test_large_f() {
  using namespace async_coro;

  const size_t before = mem_hook::num_allocated;

  move_only_function<void() noexcept(is_noexcept)> f;

  EXPECT_FALSE(f);
  bool was_called = false;

  f = [&was_called, d = 23.5, c = 3.5]() noexcept(is_noexcept) {
    was_called = d > c;

    if constexpr (!is_noexcept) {
      throw std::runtime_error("Test error");
    }
  };

  EXPECT_GT(mem_hook::num_allocated, before);

  ASSERT_TRUE(f);

  auto f2 = std::move(f);

  EXPECT_FALSE(f);
  ASSERT_TRUE(f2);

  if constexpr (is_noexcept) {
    f2();
  } else {
    try {
      f2();
    } catch (std::runtime_error& e) {
      EXPECT_STREQ(e.what(), "Test error");
    }
  }

  EXPECT_TRUE(was_called);

  f = nullptr;

  EXPECT_FALSE(f);

  EXPECT_EQ(mem_hook::num_allocated, before);
}

TEST(move_only_function, noexcept_large_f) { test_small_f<true>(); }

TEST(move_only_function, except_large_f) { test_small_f<false>(); }

TEST(move_only_function, size_check) {
  using namespace async_coro;

  EXPECT_EQ(sizeof(move_only_function<void()>), sizeof(void*) * 3);
}

template <bool is_noexcept>
static auto num_moves_large_f() {
  using namespace async_coro;

  static int num_moves = 0;
  static int num_copyes = 0;

  struct cleaner {
    ~cleaner() noexcept {
      num_moves = 0;
      num_copyes = 0;
    }
  };

  struct test_struct {
    test_struct() noexcept = default;
    test_struct(const test_struct&) { num_copyes++; }
    test_struct(test_struct&&) { num_moves++; }
    ~test_struct() noexcept = default;

    void test() const noexcept(is_noexcept) {
      EXPECT_DOUBLE_EQ(a, 23.1);
      EXPECT_DOUBLE_EQ(b, 13.3);
      EXPECT_DOUBLE_EQ(c, 14.6);
    }

    double a = 23.1;
    double b = 13.3;
    double c = 14.6;
  };

  cleaner clear{};

  move_only_function<void() noexcept(is_noexcept)> f;

  EXPECT_EQ(num_moves, 0);

  f = [tst = test_struct{}]() noexcept(is_noexcept) { tst.test(); };

  EXPECT_EQ(num_moves, 1);

  auto f2 = std::move(f);

  EXPECT_EQ(num_moves, 1);

  ASSERT_TRUE(f2);

  f2();

  f2 = nullptr;

  EXPECT_EQ(num_moves, 1);
  EXPECT_EQ(num_copyes, 0);
}

TEST(move_only_function, num_moves_large_f_noexcept) {
  num_moves_large_f<true>();
}

TEST(move_only_function, num_moves_large_f_except) {
  num_moves_large_f<true>();
}

template <bool is_noexcept, size_t add_size>
static auto num_moves_small_f() {
  using namespace async_coro;

  static int num_moves = 0;
  static int num_copyes = 0;

  struct cleaner {
    ~cleaner() noexcept {
      num_moves = 0;
      num_copyes = 0;
    }
  };

  struct test_struct {
    test_struct() noexcept = default;
    test_struct(const test_struct&) { num_copyes++; }
    test_struct(test_struct&&) noexcept { num_moves++; }
    ~test_struct() noexcept = default;

    void test() const noexcept(is_noexcept) { EXPECT_GT(num_moves, 0); }

    void* _fxs[add_size / sizeof(void*) + 1];
  };

  cleaner clear{};

  move_only_function<void() noexcept(is_noexcept), sizeof(void*) + add_size> f;

  EXPECT_EQ(num_moves, 0);

  f = [tst = test_struct{}]() noexcept(is_noexcept) { tst.test(); };

  EXPECT_EQ(num_moves, 1);

  auto f2 = std::move(f);

  EXPECT_EQ(num_moves, 2);

  ASSERT_TRUE(f2);

  f2();

  f2 = nullptr;

  EXPECT_EQ(num_moves, 2);
  EXPECT_EQ(num_copyes, 0);
}

TEST(move_only_function, num_moves_small_f_noexcept) {
  num_moves_small_f<true, 0>();
}

TEST(move_only_function, num_moves_small_f_except) {
  num_moves_small_f<false, 0>();
}

TEST(move_only_function, num_moves_small_f_exra_size_noexcept) {
  num_moves_small_f<true, sizeof(void*) + 4>();
}

TEST(move_only_function, num_moves_small_f_exra_size_except) {
  num_moves_small_f<false, sizeof(void*) + 4>();
}

TEST(move_only_function, forward_value) {
  using namespace async_coro;

  static int num_moves = 0;
  static int num_copyes = 0;

  struct cleaner {
    ~cleaner() noexcept {
      num_moves = 0;
      num_copyes = 0;
    }
  };

  struct test_struct {
    test_struct() noexcept = default;
    test_struct(const test_struct&) { num_copyes++; }
    test_struct(test_struct&&) { num_moves++; }
    ~test_struct() noexcept = default;
  };

  cleaner clean{};

  move_only_function<void(test_struct)> f = [](auto s) {
    EXPECT_NE(&s, nullptr);
  };

  EXPECT_EQ(num_copyes, 0);
  EXPECT_EQ(num_moves, 0);

  f({});

  EXPECT_EQ(num_copyes, 0);
  EXPECT_EQ(num_moves, 1);
}

TEST(move_only_function, return_value) {
  using namespace async_coro;

  static int num_moves = 0;
  static int num_copyes = 0;

  struct cleaner {
    ~cleaner() noexcept {
      num_moves = 0;
      num_copyes = 0;
    }
  };

  struct test_struct {
    test_struct() noexcept = default;
    test_struct(const test_struct&) { num_copyes++; }
    test_struct(test_struct&&) { num_moves++; }
    ~test_struct() noexcept = default;
  };

  cleaner clean{};

  move_only_function<test_struct(int)> f = [](auto s) {
    EXPECT_EQ(s, 3);
    return test_struct{};
  };

  EXPECT_EQ(num_copyes, 0);
  EXPECT_EQ(num_moves, 0);

  f(3);

  EXPECT_EQ(num_copyes, 0);
  EXPECT_EQ(num_moves, 0);
}

TEST(move_only_function, rvalue_forward) {
  using namespace async_coro;

  static int num_moves = 0;
  static int num_copyes = 0;

  struct cleaner {
    ~cleaner() noexcept {
      num_moves = 0;
      num_copyes = 0;
    }
  };

  struct test_struct {
    test_struct() noexcept = default;
    test_struct(const test_struct&) { num_copyes++; }
    test_struct(test_struct&&) { num_moves++; }
    ~test_struct() noexcept = default;
  };

  cleaner clean{};

  {
    move_only_function<void(test_struct&&, int)> f = [](auto&& s, auto i) {
      EXPECT_NE(&s, nullptr);
      EXPECT_EQ(i, 7);
    };

    EXPECT_EQ(num_copyes, 0);
    EXPECT_EQ(num_moves, 0);

    f({}, 7);

    EXPECT_EQ(num_copyes, 0);
    EXPECT_EQ(num_moves, 0);
  }

  {
    move_only_function<void(test_struct&&)> f = [](auto s) {
      EXPECT_NE(&s, nullptr);
    };

    EXPECT_EQ(num_copyes, 0);
    EXPECT_EQ(num_moves, 0);

    f({});

    EXPECT_EQ(num_copyes, 0);
    EXPECT_EQ(num_moves, 1);
  }
}

TEST(move_only_function, mutable_f) {
  using namespace async_coro;

  {
    int num_calls = 0;

    move_only_function<void()> f = [called = false, &num_calls]() mutable {
      if (!called) {
        called = true;
        num_calls++;
      }
    };

    EXPECT_EQ(num_calls, 0);

    f();

    EXPECT_EQ(num_calls, 1);

    f();

    EXPECT_EQ(num_calls, 1);
  }
}

TEST(move_only_function, mutable_noexcept_f) {
  using namespace async_coro;

  {
    int num_calls = 0;

    move_only_function<void() noexcept> f = [called = false, &num_calls]() mutable noexcept {
      if (!called) {
        called = true;
        num_calls++;
      }
    };

    EXPECT_EQ(num_calls, 0);

    f();

    EXPECT_EQ(num_calls, 1);

    f();

    EXPECT_EQ(num_calls, 1);
  }
}

TEST(move_only_function, lref_arg) {
  using namespace async_coro;

  static int val = 0;

  move_only_function<void(int&)> f = [](auto& i) mutable noexcept {
    EXPECT_EQ(&i, &val);
  };

  f(val);
}

TEST(move_only_function, const_lref_arg) {
  using namespace async_coro;

  static int val = 0;

  move_only_function<void(const int&)> f = [](auto& i) mutable noexcept {
    EXPECT_EQ(&i, &val);
  };

  f(val);
}

TEST(move_only_function, rref_arg) {
  using namespace async_coro;

  static int val = 0;

  move_only_function<void(int&&)> f = [](auto&& i) mutable noexcept {
    EXPECT_EQ(&i, &val);
  };

  f(std::move(val));
}

TEST(move_only_function, val_arg) {
  using namespace async_coro;

  static int val = 0;

  move_only_function<void(int)> f = [](auto&& i) mutable noexcept {
    EXPECT_NE(&i, &val);
  };

  f(val);
}

TEST(move_only_function, speed_invoke_function) {
  using namespace async_coro;

  static int val = 0;

  move_only_function<void(int)> f1 = [](auto&& i) mutable noexcept {
    EXPECT_NE(&i, &val);
  };

  std::function<void(int)> f2 = [](auto&& i) mutable noexcept {
    EXPECT_NE(&i, &val);
  };

  const auto t1 = std::chrono::steady_clock::now();
  for (int i = 0; i < 100000; i++) {
    f1(val);
  }
  const auto f1_time = std::chrono::steady_clock::now() - t1;

  const auto t2 = std::chrono::steady_clock::now();
  for (int i = 0; i < 100000; i++) {
    f2(val);
  }
  const auto f2_time = std::chrono::steady_clock::now() - t2;

  std::cout << "move_only_function time: " << f1_time.count() << ", std::function time: " << f2_time.count() << std::endl;
}
