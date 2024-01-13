#include <gtest/gtest.h>
#include <async_coro/internal/move_only_function.h>

TEST(move_only_function, noexcept_small_f) {
    using namespace async_coro::internal;

	move_only_function<void() noexcept> f;

	EXPECT_FALSE(f);
	static bool was_called = false;
	was_called = false;

	f = []() noexcept {
		was_called = true;
	};

	ASSERT_TRUE(f);

	f();

	EXPECT_TRUE(was_called);

	f = nullptr;

	EXPECT_FALSE(f);
}

TEST(move_only_function, noexcept_large_f) {
    using namespace async_coro::internal;

	move_only_function<void() noexcept> f;

	EXPECT_FALSE(f);
	bool was_called = false;

	f = [&was_called, d = 23.5, c = 3.5]() noexcept {
		was_called = d > c;
	};

	ASSERT_TRUE(f);

	f();

	EXPECT_TRUE(was_called);

	f = nullptr;

	EXPECT_FALSE(f);
}

TEST(move_only_function, except_small_f) {
    using namespace async_coro::internal;

	move_only_function<void()> f;

	EXPECT_FALSE(f);
	static bool was_called = false;
	was_called = false;

	f = []() {
		was_called = true;
	};

	ASSERT_TRUE(f);

	f();

	EXPECT_TRUE(was_called);

	f = nullptr;

	EXPECT_FALSE(f);
}

TEST(move_only_function, except_large_f) {
    using namespace async_coro::internal;

	move_only_function<void()> f;

	EXPECT_FALSE(f);
	bool was_called = false;

	f = [&was_called, d = 23.5, c = 3.5]() {
		was_called = d > c;
	};

	ASSERT_TRUE(f);

	f();

	EXPECT_TRUE(was_called);

	f = nullptr;

	EXPECT_FALSE(f);
}
