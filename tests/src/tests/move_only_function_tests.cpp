#include <gtest/gtest.h>
#include <async_coro/move_only_function.h>
#include <stdexcept>

namespace move_function_test_details {
	std::atomic_size_t num_allocated = 0;
}

void* operator new (std::size_t count) {
	move_function_test_details::num_allocated += count + sizeof(std::size_t);
	auto mem = static_cast<std::size_t*>(std::malloc(count + sizeof(std::size_t)));
	*mem = count;
	return mem + 1;
}

void operator delete(void* ptr) noexcept {
	auto mem = static_cast<std::size_t*>(ptr) - 1;
	move_function_test_details::num_allocated -= *mem + sizeof(std::size_t);
	std::free(mem);
}

template<bool is_noexcept>
static auto test_small_f() {
	using namespace async_coro;

	const size_t before = move_function_test_details::num_allocated;

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

	EXPECT_EQ(before, move_function_test_details::num_allocated);

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

	EXPECT_EQ(before, move_function_test_details::num_allocated);
}

TEST(move_only_function, except_small_f) {
    test_small_f<false>();
}

TEST(move_only_function, noexcept_small_f) {
    test_small_f<true>();
}


template<bool is_noexcept>
static auto test_large_f() {
	using namespace async_coro;

	const size_t before = move_function_test_details::num_allocated;

	move_only_function<void() noexcept(is_noexcept)> f;

	EXPECT_FALSE(f);
	bool was_called = false;

	f = [&was_called, d = 23.5, c = 3.5]() noexcept(is_noexcept) {
		was_called = d > c;

		if constexpr (!is_noexcept) {
			throw std::runtime_error("Test error");
		}
	};

	EXPECT_GT(move_function_test_details::num_allocated, before);

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

	EXPECT_EQ(move_function_test_details::num_allocated, before);
}

TEST(move_only_function, noexcept_large_f) {
    test_small_f<true>();
}

TEST(move_only_function, except_large_f) {
    test_small_f<false>();
}


TEST(move_only_function, size_check) {
	using namespace async_coro;

	EXPECT_EQ(sizeof(move_only_function<void()>), sizeof(void*) * 3);
}
