//
// Created by usatiynyan.
//

#include "sl/exec/st.hpp"

#include <gtest/gtest.h>

namespace sl::exec {

TEST(st, singleTask) {
    st_executor executor;
    std::size_t counter = 0;

    schedule(executor, [&counter](generic_executor&) noexcept { ++counter; });
    ASSERT_EQ(counter, 0);

    EXPECT_EQ(executor.execute_once(), 1);
    ASSERT_EQ(counter, 1);
}

TEST(st, manyTasks) {
    st_executor executor;
    std::size_t counter = 0;
    constexpr std::size_t expected = 1000;

    for (std::size_t i = 0; i != expected; ++i) {
        schedule(executor, [&counter](generic_executor&) noexcept { ++counter; });
    }
    ASSERT_EQ(counter, 0);

    EXPECT_EQ(executor.execute_once(), 1);
    ASSERT_EQ(counter, 1);

    EXPECT_EQ(executor.execute_at_most(99), 99);
    ASSERT_EQ(counter, 100);

    EXPECT_EQ(executor.execute_batch(), 900);
    ASSERT_EQ(counter, expected);

    EXPECT_EQ(executor.execute_once(), 0);
    EXPECT_EQ(executor.execute_at_most(99), 0);
    EXPECT_EQ(executor.execute_batch(), 0);
    ASSERT_EQ(counter, expected);
}

void nesting_task(generic_executor& executor, std::size_t& counter, std::size_t expected) noexcept {
    schedule(executor, [&counter, expected](generic_executor& executor_inner) noexcept {
        if (counter < expected) {
            ++counter;
            nesting_task(executor_inner, counter, expected);
        }
    });
}

TEST(st, nestingTasks) {
    st_executor executor;
    std::size_t counter = 0;
    constexpr std::size_t expected = 1000;
    nesting_task(executor, counter, expected);

    EXPECT_EQ(executor.execute_once(), 1);
    ASSERT_EQ(counter, 1);

    EXPECT_EQ(executor.execute_at_most(99), 99);
    ASSERT_EQ(counter, 100);

    EXPECT_EQ(executor.execute_batch(), 1);
    ASSERT_EQ(counter, 101);

    executor.execute_until([&] { return counter < expected; });
    ASSERT_EQ(counter, expected);

    EXPECT_EQ(executor.execute_once(), 0);
    EXPECT_EQ(executor.execute_at_most(99), 0);
    EXPECT_EQ(executor.execute_batch(), 0);
    ASSERT_EQ(counter, expected);
}

} // namespace sl::exec
