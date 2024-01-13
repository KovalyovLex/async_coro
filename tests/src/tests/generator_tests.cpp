#include <gtest/gtest.h>
#include <async_coro/generator.h>

TEST(generator, yeld_simple) {
    auto routine = []() -> async_coro::generator<int> {
        co_yield 1;
        co_yield 2;
    }();

    ASSERT_FALSE(routine.is_finished());
    ASSERT_EQ(routine.get_value(), nullptr);
    routine.move_next();
    ASSERT_NE(routine.get_value(), nullptr);
    ASSERT_EQ(*routine.get_value(), 1);
    routine.move_next();
    ASSERT_NE(routine.get_value(), nullptr);
    ASSERT_EQ(*routine.get_value(), 2);
    ASSERT_FALSE(routine.is_finished());
    routine.move_next();
    ASSERT_TRUE(routine.is_finished());
    ASSERT_EQ(routine.get_value(), nullptr);
}

TEST(generator, yeld_internal) {
    const auto createRoutine2 = []() -> async_coro::generator<int> {
        co_yield 2;
        co_yield 3;
    };

    auto routine = [createRoutine2]() -> async_coro::generator<int> {
        co_yield 1;
        co_yield createRoutine2();
        co_yield 4;
    }();

    ASSERT_FALSE(routine.is_finished());
    ASSERT_EQ(routine.get_value(), nullptr);

    routine.move_next();
    ASSERT_FALSE(routine.is_finished());
    ASSERT_NE(routine.get_value(), nullptr);
    ASSERT_EQ(*routine.get_value(), 1);

    routine.move_next();
    ASSERT_FALSE(routine.is_finished());
    ASSERT_NE(routine.get_value(), nullptr);
    ASSERT_EQ(*routine.get_value(), 2);

    routine.move_next();
    ASSERT_FALSE(routine.is_finished());
    ASSERT_NE(routine.get_value(), nullptr);
    ASSERT_EQ(*routine.get_value(), 3);

    routine.move_next();
    ASSERT_FALSE(routine.is_finished());
    ASSERT_NE(routine.get_value(), nullptr);
    ASSERT_EQ(*routine.get_value(), 4);

    routine.move_next();
    ASSERT_TRUE(routine.is_finished());
    ASSERT_EQ(routine.get_value(), nullptr);
}

TEST(generator, void_co_yield) {
    auto routine = []() -> async_coro::generator<void> {
        co_yield {};
		co_return;
    }();

    ASSERT_FALSE(routine.is_finished());
    ASSERT_EQ(routine.get_value(), nullptr);

    routine.move_next();
    ASSERT_FALSE(routine.is_finished());

    routine.move_next();
    ASSERT_TRUE(routine.is_finished());
    ASSERT_EQ(routine.get_value(), nullptr);
}
