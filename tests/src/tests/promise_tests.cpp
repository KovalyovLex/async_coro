#include <gtest/gtest.h>
#include <async_coro/promise.h>
#include <async_coro/await_callback.h>

namespace promise_tests {
	struct coro_runner : async_coro::base_handle
	{
		template<typename T>
		void run_coroutine(async_coro::promise<T>& coro) {
			coro.get_handle(async_coro::internal::passkey{ this }).resume();
		}
	};
}

TEST(promise, await_no_wait) {
	auto routine_1 = []() -> async_coro::promise<float> {
        co_return 45.456f;
    };

	auto routine_2 = []() -> async_coro::promise<int> {
        co_return 2;
    };

    auto routine = [routine_2, routine_1]() -> async_coro::promise<int> {
		const auto res1 = co_await routine_1();
		auto routine1 = routine_1();
		auto res2 = co_await std::move(routine1);
        const auto res = co_await routine_2();
        co_return res;
    }();

    ASSERT_FALSE(routine.done());
	promise_tests::coro_runner().run_coroutine(routine);
	EXPECT_EQ(routine.get(), 2);
	EXPECT_EQ((int)routine, 2);
	ASSERT_TRUE(routine.done());
}

TEST(promise, resume_on_callback) {
	std::function<void()> continue_f;

    auto routine = [&continue_f]() -> async_coro::promise<int> {
		co_await async_coro::await_callback([&continue_f](auto f) {
			continue_f = std::move(f);
		});
        co_return 3;
    }();

    ASSERT_FALSE(routine.done());
	promise_tests::coro_runner().run_coroutine(routine);
	ASSERT_FALSE(routine.done());
	ASSERT_TRUE(continue_f);
	continue_f();
	ASSERT_TRUE(routine.done());
	EXPECT_EQ(routine.get(), 3);
}

TEST(promise, resume_on_callback_reuse) {
	std::function<void()> continue_f;

    auto routine = [&continue_f]() -> async_coro::promise<int> {
		auto cnt = async_coro::await_callback([&continue_f](auto f) {
			continue_f = std::move(f);
		});
		co_await cnt;

		co_await cnt;

        co_return 2;
    }();

    ASSERT_FALSE(routine.done());
	promise_tests::coro_runner().run_coroutine(routine);
	ASSERT_FALSE(routine.done());
	ASSERT_TRUE(continue_f);
	std::exchange(continue_f, {})();
	ASSERT_FALSE(routine.done());
	ASSERT_TRUE(continue_f);
	std::exchange(continue_f, {})();
	ASSERT_TRUE(routine.done());
	EXPECT_EQ(routine.get(), 2);
}
