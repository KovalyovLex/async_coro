#include <gtest/gtest.h>
#include <async_coro/task.h>
#include <async_coro/scheduler.h>
#include <async_coro/await_callback.h>

namespace task_tests {
	struct coro_runner
	{
		template<typename T>
		void run_coroutine(async_coro::task<T> coro) {
			_scheduler.start_task(std::move(coro));
		}

		async_coro::scheduler _scheduler;
	};
}

TEST(task, await_no_wait) {
	auto routine_1 = []() -> async_coro::task<float> {
        co_return 45.456f;
    };

	auto routine_2 = []() -> async_coro::task<int> {
        co_return 2;
    };

    auto routine = [routine_2, routine_1]() -> async_coro::task<int> {
		const auto res1 = co_await routine_1();
		auto routine1 = routine_1();
		auto res2 = co_await std::move(routine1);
        const auto res = co_await routine_2();
        co_return res;
    }();

	async_coro::scheduler scheduler;

    ASSERT_FALSE(routine.done());
	auto handle = scheduler.start_task(std::move(routine));
	ASSERT_TRUE(handle.done());
	EXPECT_EQ(handle.get(), 2);
	EXPECT_EQ((int)handle, 2);
}

TEST(task, resume_on_callback_deep) {
	std::function<void()> continue_f;

	auto routine_1 = [&continue_f]() -> async_coro::task<float> {
		co_await async_coro::await_callback([&continue_f](auto f) {
			continue_f = std::move(f);
		});
        co_return 45.456f;
    };

	auto routine_2 = [routine_1]() -> async_coro::task<int> {
		const auto res = co_await routine_1();
        co_return (int)(res);
    };

    auto routine = [routine_2]() -> async_coro::task<int> {
		const auto res = co_await routine_2();
        co_return res;
    }();

	async_coro::scheduler scheduler;

    ASSERT_FALSE(routine.done());
	auto handle = scheduler.start_task(std::move(routine));
	ASSERT_FALSE(handle.done());
	ASSERT_TRUE(continue_f);
	continue_f();
	ASSERT_TRUE(handle.done());
	EXPECT_EQ(handle.get(), 45);
}


TEST(task, resume_on_callback) {
	std::function<void()> continue_f;

    auto routine = [&continue_f]() -> async_coro::task<int> {
		co_await async_coro::await_callback([&continue_f](auto f) {
			continue_f = std::move(f);
		});
        co_return 3;
    }();

	async_coro::scheduler scheduler;

    ASSERT_FALSE(routine.done());
	auto handle = scheduler.start_task(std::move(routine));
	ASSERT_FALSE(handle.done());
	ASSERT_TRUE(continue_f);
	continue_f();
	ASSERT_TRUE(handle.done());
	EXPECT_EQ(handle.get(), 3);
}

TEST(task, resume_on_callback_reuse) {
	std::function<void()> continue_f;

    auto routine = [&continue_f]() -> async_coro::task<int> {
		auto cnt = async_coro::await_callback([&continue_f](auto f) {
			continue_f = std::move(f);
		});
		co_await cnt;

		co_await cnt;

        co_return 2;
    }();

	async_coro::scheduler scheduler;

    ASSERT_FALSE(routine.done());
	auto handle = scheduler.start_task(std::move(routine));
	ASSERT_FALSE(handle.done());
	ASSERT_TRUE(continue_f);
	std::exchange(continue_f, {})();
	ASSERT_FALSE(handle.done());
	ASSERT_TRUE(continue_f);
	std::exchange(continue_f, {})();
	ASSERT_TRUE(handle.done());
	EXPECT_EQ(handle.get(), 2);
}
