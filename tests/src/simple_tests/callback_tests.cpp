#include <async_coro/callback.h>
#include <gtest/gtest.h>

TEST(callback, create_and_execute) {
  int result{0};
  auto callback = async_coro::callback<int(int)>::allocate([&](int value) noexcept {
    result = value;
    return value;
  });

  callback->execute(10);
  callback->destroy();
  EXPECT_EQ(result, 10);
}

TEST(callback, allocate_callback_deduced) {
  int result{0};
  auto callback = async_coro::allocate_callback([&](int value) {
    result = value;
    return value;
  });

  callback->execute(10);
  EXPECT_EQ(result, 10);
}

TEST(callback, allocate_callback_deduced_noexcept) {
  float result{0};
  auto callback = async_coro::allocate_callback([&](int value) noexcept {
    result = static_cast<float>(value);
    return result;
  });

  EXPECT_FLOAT_EQ(callback->execute(10), 10.0f);
  EXPECT_FLOAT_EQ(result, 10.0f);
}

TEST(callback, check_destructor) {
  bool destructed = false;
  struct destructor_checker {
    bool& _destructed;
    destructor_checker(bool& d) : _destructed(d) {}
    ~destructor_checker() { _destructed = true; }
  };

  {
    auto callback = async_coro::allocate_callback([c = destructor_checker{destructed}]() {});
  }

  EXPECT_TRUE(destructed);
}

TEST(callback, check_destructor_noexcept) {
  bool destructed = false;
  struct destructor_checker {
    bool& _destructed;
    destructor_checker(bool& d) : _destructed(d) {}
    ~destructor_checker() { _destructed = true; }
  };

  {
    auto callback = async_coro::allocate_callback([c = destructor_checker{destructed}]() noexcept {});
  }

  EXPECT_TRUE(destructed);
}

TEST(callback, check_destructor_manual_destroy) {
  bool destructed = false;
  struct destructor_checker {
    bool& _destructed;
    destructor_checker(bool& d) : _destructed(d) {}
    ~destructor_checker() { _destructed = true; }
  };

  auto callback = async_coro::callback<void()>::allocate([c = destructor_checker{destructed}]() noexcept {});
  callback->destroy();

  EXPECT_TRUE(destructed);
}

TEST(callback, different_argument_types) {
  int i = 0;
  double d = 0.0;
  std::string s;

  auto cb = async_coro::allocate_callback(
      [&](int val1, double val2, const std::string& val3) {
        i = val1;
        d = val2;
        s = val3;
      });

  cb->execute(42, 3.14, "hello");

  EXPECT_EQ(i, 42);
  EXPECT_DOUBLE_EQ(d, 3.14);
  EXPECT_EQ(s, "hello");
}

TEST(callback, different_argument_types_noexcept) {
  int i = 0;
  double d = 0.0;
  std::string s;

  auto cb = async_coro::allocate_callback(
      [&](int val1, double val2, const std::string& val3) noexcept {
        i = val1;
        d = val2;
        s = val3;
      });

  cb->execute(42, 3.14, "hello");

  EXPECT_EQ(i, 42);
  EXPECT_DOUBLE_EQ(d, 3.14);
  EXPECT_EQ(s, "hello");
}

TEST(callback, different_captured_types) {
  int captured_by_value = 10;
  int captured_by_ref = 20;
  int result = 0;

  auto cb = async_coro::allocate_callback(
      [captured_by_value, &captured_by_ref, &result](int arg) {
        result = captured_by_value + captured_by_ref + arg;
      });

  captured_by_ref = 30;  // Modify after capture
  cb->execute(40);

  EXPECT_EQ(result, 10 + 30 + 40);
}

TEST(callback, different_captured_types_noexcept) {
  int captured_by_value = 10;
  int captured_by_ref = 20;
  int result = 0;

  auto cb = async_coro::allocate_callback(
      [captured_by_value, &captured_by_ref, &result](int arg) noexcept {
        result = captured_by_value + captured_by_ref + arg;
      });

  captured_by_ref = 30;  // Modify after capture
  cb->execute(40);

  EXPECT_EQ(result, 10 + 30 + 40);
}
