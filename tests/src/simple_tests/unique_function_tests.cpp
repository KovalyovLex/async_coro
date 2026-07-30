#include <async_coro/utils/unique_function.h>
#include <gtest/gtest.h>
#include <utils/memory_hooks.h>

#include <chrono>
#include <iostream>
#include <stdexcept>

template <bool is_noexcept>
static auto test_small_f() {
  using namespace async_coro;

  const std::size_t before = mem_hook::num_allocated;

  unique_function<void() noexcept(is_noexcept)> f;

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

  EXPECT_FALSE(f);  // NOLINT(*-move*)
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

TEST(unique_function, except_small_f) { test_small_f<false>(); }

TEST(unique_function, noexcept_small_f) { test_small_f<true>(); }

template <bool is_noexcept>
static auto test_large_f() {
  using namespace async_coro;

  const std::size_t before = mem_hook::num_allocated;

  unique_function<void() noexcept(is_noexcept)> f;

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

TEST(unique_function, noexcept_large_f) { test_small_f<true>(); }

TEST(unique_function, except_large_f) { test_small_f<false>(); }

TEST(unique_function, size_check) {
  using namespace async_coro;

  EXPECT_EQ(sizeof(unique_function<void()>), sizeof(void*) * 4);
}

template <bool is_noexcept>
static auto num_moves_large_f() {
  using namespace async_coro;

  static int num_moves = 0;
  static int num_copies = 0;

  struct cleaner {
    ~cleaner() noexcept {
      num_moves = 0;
      num_copies = 0;
    }
  };

  struct test_struct {
    test_struct() noexcept = default;
    test_struct(const test_struct& /*unused*/) { num_copies++; }
    test_struct(test_struct&& /*unused*/) noexcept { num_moves++; }
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

  unique_function<void() noexcept(is_noexcept)> f;

  EXPECT_EQ(num_moves, 0);

  f = [tst = test_struct{}]() noexcept(is_noexcept) { tst.test(); };

  EXPECT_EQ(num_moves, 1);

  auto f2 = std::move(f);

  EXPECT_EQ(num_moves, 1);

  ASSERT_TRUE(f2);

  f2();

  f2 = nullptr;

  EXPECT_EQ(num_moves, 1);
  EXPECT_EQ(num_copies, 0);
}

TEST(unique_function, num_moves_large_f_noexcept) {
  num_moves_large_f<true>();
}

TEST(unique_function, num_moves_large_f_except) {
  num_moves_large_f<true>();
}

template <bool is_noexcept, std::size_t add_size>
static auto num_moves_small_f() {
  using namespace async_coro;

  static int num_moves = 0;
  static int num_copies = 0;

  struct cleaner {
    ~cleaner() noexcept {
      num_moves = 0;
      num_copies = 0;
    }
  };

  struct test_struct {
    test_struct() noexcept = default;
    test_struct(const test_struct& /*unused*/) { num_copies++; }
    test_struct(test_struct&& /*unused*/) noexcept { num_moves++; }
    ~test_struct() noexcept = default;

    void test() const noexcept(is_noexcept) { EXPECT_GT(num_moves, 0); }

    void* _fxs[(add_size / sizeof(void*)) + 1]{};  // NOLINT(*-array*)
  };

  cleaner clear{};

  unique_function<void() noexcept(is_noexcept), sizeof(void*) + add_size> f;

  EXPECT_EQ(num_moves, 0);

  f = [tst = test_struct{}]() noexcept(is_noexcept) { tst.test(); };

  EXPECT_EQ(num_moves, 1);

  auto f2 = std::move(f);

  EXPECT_EQ(num_moves, 2);

  ASSERT_TRUE(f2);

  f2();

  f2 = nullptr;

  EXPECT_EQ(num_moves, 2);
  EXPECT_EQ(num_copies, 0);
}

TEST(unique_function, num_moves_small_f_noexcept) {
  num_moves_small_f<true, 0>();
}

TEST(unique_function, num_moves_small_f_except) {
  num_moves_small_f<false, 0>();
}

TEST(unique_function, num_moves_small_f_exra_size_noexcept) {
  num_moves_small_f<true, sizeof(void*) + 4>();
}

TEST(unique_function, num_moves_small_f_exra_size_except) {
  num_moves_small_f<false, sizeof(void*) + 4>();
}

TEST(unique_function, forward_value) {
  using namespace async_coro;

  static int num_moves = 0;
  static int num_copies = 0;

  struct cleaner {
    ~cleaner() noexcept {
      num_moves = 0;
      num_copies = 0;
    }
  };

  struct test_struct {
    test_struct() noexcept = default;
    test_struct(const test_struct& /*unused*/) { num_copies++; }
    test_struct(test_struct&& /*unused*/) noexcept { num_moves++; }
    ~test_struct() noexcept = default;
  };

  cleaner clean{};

  unique_function<void(test_struct)> f = [](auto s) {  // NOLINT(*-value-param*)
    EXPECT_NE(&s, nullptr);
  };

  EXPECT_EQ(num_copies, 0);
  EXPECT_EQ(num_moves, 0);

  f({});

  EXPECT_EQ(num_copies, 0);
  EXPECT_EQ(num_moves, 1);
}

TEST(unique_function, return_value) {
  using namespace async_coro;

  static int num_moves = 0;
  static int num_copies = 0;

  struct cleaner {
    ~cleaner() noexcept {
      num_moves = 0;
      num_copies = 0;
    }
  };

  struct test_struct {
    test_struct() noexcept = default;
    test_struct(const test_struct& /*unused*/) { num_copies++; }
    test_struct(test_struct&& /*unused*/) noexcept { num_moves++; }
    ~test_struct() noexcept = default;
  };

  cleaner clean{};

  unique_function<test_struct(int)> f = [](auto s) {
    EXPECT_EQ(s, 3);
    return test_struct{};
  };

  EXPECT_EQ(num_copies, 0);
  EXPECT_EQ(num_moves, 0);

  f(3);

  EXPECT_EQ(num_copies, 0);
  EXPECT_EQ(num_moves, 0);
}

TEST(unique_function, rvalue_forward) {
  using namespace async_coro;

  static int num_moves = 0;
  static int num_copies = 0;

  struct cleaner {
    ~cleaner() noexcept {
      num_moves = 0;
      num_copies = 0;
    }
  };

  struct test_struct {
    test_struct() noexcept = default;
    test_struct(const test_struct& /*unused*/) { num_copies++; }
    test_struct(test_struct&& /*unused*/) noexcept { num_moves++; }
    ~test_struct() noexcept = default;
  };

  cleaner clean{};

  {
    unique_function<void(test_struct&&, int)> move_fn = [](auto&& test_struct_ref, int count) {
      EXPECT_NE(&test_struct_ref, nullptr);
      EXPECT_EQ(count, 7);  // NOLINT(readability-magic-numbers)
    };

    EXPECT_EQ(num_copies, 0);
    EXPECT_EQ(num_moves, 0);

    move_fn({}, 7);  // NOLINT(readability-magic-numbers)

    EXPECT_EQ(num_copies, 0);
    EXPECT_EQ(num_moves, 0);
  }

  {
    unique_function<void(test_struct&&)> move_only_fn = [](auto struct_value) {  // NOLINT(*-value-param*,readability-identifier-length)
      EXPECT_NE(&struct_value, nullptr);
    };

    EXPECT_EQ(num_copies, 0);
    EXPECT_EQ(num_moves, 0);

    move_only_fn({});

    EXPECT_EQ(num_copies, 0);
    EXPECT_EQ(num_moves, 1);
  }
}

TEST(unique_function, mutable_f) {
  using namespace async_coro;

  {
    int num_calls = 0;

    unique_function<void()> call_counter = [called = false, &num_calls]() mutable {
      if (!called) {
        called = true;
        num_calls++;
      }
    };

    EXPECT_EQ(num_calls, 0);

    call_counter();

    EXPECT_EQ(num_calls, 1);

    call_counter();

    EXPECT_EQ(num_calls, 1);
  }
}

TEST(unique_function, mutable_noexcept_f) {
  using namespace async_coro;

  {
    int num_calls = 0;

    unique_function<void() noexcept> noexcept_call_counter = [called = false, &num_calls]() mutable noexcept {
      if (!called) {
        called = true;
        num_calls++;
      }
    };

    EXPECT_EQ(num_calls, 0);

    noexcept_call_counter();

    EXPECT_EQ(num_calls, 1);

    noexcept_call_counter();

    EXPECT_EQ(num_calls, 1);
  }
}

TEST(unique_function, lref_arg) {
  using namespace async_coro;

  static int val = 0;

  unique_function<void(int&)> ref_fn = [](auto& int_ref) mutable noexcept {
    EXPECT_EQ(&int_ref, &val);
  };

  ref_fn(val);
}

TEST(unique_function, const_lref_arg) {
  using namespace async_coro;

  static int val = 0;

  unique_function<void(const int&)> const_ref_fn = [](auto& int_ref) mutable noexcept {
    EXPECT_EQ(&int_ref, &val);
  };

  const_ref_fn(val);
}

TEST(unique_function, rref_arg) {
  using namespace async_coro;

  static int val = 0;

  unique_function<void(int&&)> rref_fn = [](auto&& int_rref) mutable noexcept {
    EXPECT_EQ(&int_rref, &val);
  };

  rref_fn(std::move(val));
}

TEST(unique_function, val_arg) {
  using namespace async_coro;

  static int val = 0;

  unique_function<void(int)> value_fn = [](auto&& int_value) mutable noexcept {
    EXPECT_NE(&int_value, &val);
  };

  value_fn(val);
}

TEST(unique_function, speed_invoke_function) {
  using namespace async_coro;

  static int val = 0;

  constexpr int benchmark_iterations = 100000;  // NOLINT(cppcoreguidelines-avoid-magic-numbers)

  unique_function<void(int)> async_fn = [](auto&& int_ref) mutable noexcept {
    EXPECT_NE(&int_ref, &val);
  };

  std::function<void(int)> std_fn = [](auto&& int_ref) mutable noexcept {
    EXPECT_NE(&int_ref, &val);
  };

  const auto benchmark_start = std::chrono::steady_clock::now();
  for (int iter = 0; iter < benchmark_iterations; iter++) {
    async_fn(val);
  }
  const auto async_duration = std::chrono::steady_clock::now() - benchmark_start;

  const auto std_benchmark_start = std::chrono::steady_clock::now();
  for (int iter = 0; iter < benchmark_iterations; iter++) {
    std_fn(val);
  }
  const auto std_duration = std::chrono::steady_clock::now() - std_benchmark_start;

  std::cout << "unique_function time: " << async_duration.count() << ", std::function time: " << std_duration.count() << '\n';
}

template <class FxSig, class Fx>
using invocable = async_coro::internal::is_invocable_by_signature<FxSig, Fx>;

int testF() { return 0; }
bool testFS(int /*unused*/) { return false; }

TEST(unique_function, compilation) {
  auto testF1 = []() -> bool {
    return false;
  };

  auto testF2 = [](auto) noexcept -> bool {
    return false;
  };

  auto test_fn_capture = [capture_val = 3]() mutable {
    EXPECT_EQ(capture_val, 3);
    capture_val = 4;
  };

  static_assert(invocable<void(), decltype(testF1)>::value);
  static_assert(invocable<void(), decltype(test_fn_capture)>::value);
  static_assert(!invocable<void(), decltype(testF2)>::value);
  static_assert(invocable<void(int) noexcept, decltype(testF2)>::value);
  static_assert(!invocable<void() noexcept, decltype(testF1)>::value);
}

TEST(unique_function, store_storage) {
  using namespace async_coro;

  static int num_alive = 0;

  struct move_struct {
    move_struct() {
      num_alive++;
    }
    move_struct(const move_struct&) = delete;
    move_struct(move_struct&& /*unused*/) noexcept {
      num_alive++;
    }
    move_struct& operator=(const move_struct&) = delete;
    move_struct& operator=(move_struct&&) = delete;
    ~move_struct() noexcept {
      num_alive--;
    }
  };

  {
    unique_function_storage storage;

    {
      constexpr float expected_result = 22.5F;  // NOLINT(cppcoreguidelines-avoid-magic-numbers)
      unique_function<float(int)> storage_fn = [to_move = move_struct{}](auto&& int_param) noexcept {
        (void)to_move;
        EXPECT_EQ(int_param, 34);

        return expected_result;
      };

      EXPECT_EQ(num_alive, 1);

      EXPECT_FLOAT_EQ(storage_fn.move_to_storage_and_call(storage, 34), expected_result);
    }

    EXPECT_EQ(num_alive, 1);
  }

  EXPECT_EQ(num_alive, 0);
}
