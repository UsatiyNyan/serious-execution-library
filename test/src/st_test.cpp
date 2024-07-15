//
// Created by usatiynyan.
//

#include "sl/exec/st.hpp"

#include <gtest/gtest.h>

namespace sl::exec {

TEST(st, singleTask) {
    st::executor executor;
    std::size_t counter = 0;

    schedule(executor, [&counter] noexcept { ++counter; });
    ASSERT_EQ(counter, 0);

    EXPECT_EQ(executor.execute_once(), 1);
    ASSERT_EQ(counter, 1);
}

TEST(st, manyTasks) {
    st::executor executor;
    std::size_t counter = 0;
    constexpr std::size_t expected = 1000;

    for (std::size_t i = 0; i != expected; ++i) {
        schedule(executor, [&counter] noexcept { ++counter; });
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
    schedule(executor, [&executor, &counter, expected] noexcept {
        if (counter < expected) {
            ++counter;
            nesting_task(executor, counter, expected);
        }
    });
}

TEST(st, nestingTasks) {
    st::executor executor;
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

TEST(st, asyncOne) {
    st::executor executor;
    std::size_t counter = 0;

    auto coro = [&counter] -> async<void> {
        ++counter;
        co_return;
    };
    schedule(executor, coro());
    ASSERT_EQ(counter, 0);

    EXPECT_EQ(executor.execute_batch(), 1);
    ASSERT_EQ(counter, 1);
}

TEST(st, asyncMany) {
    st::executor executor;
    std::size_t counter = 0;
    constexpr std::size_t expected = 1000;

    auto inner = [] -> async<std::size_t> { co_return 1; };
    auto outer = [&counter, inner] -> async<void> {
        for (std::size_t i = 0; i < expected; ++i) {
            counter += co_await inner();
        }
        co_return;
    };
    schedule(executor, outer());
    ASSERT_EQ(counter, 0);

    EXPECT_EQ(executor.execute_batch(), 1);
    ASSERT_EQ(counter, expected);
}

async<std::size_t> nesting_coro(std::size_t expected) {
    if (expected == 0) {
        co_return 0;
    }
    co_return 1 + co_await nesting_coro(expected - 1);
}

TEST(st, asyncNesting) {
    st::executor executor;
    constexpr std::size_t expected = 1'000'000;
    std::size_t result = 0;

    schedule(executor, [&result] -> async<void> { result = co_await nesting_coro(expected); }());
    ASSERT_EQ(result, 0);

    EXPECT_EQ(executor.execute_batch(), 1);
    ASSERT_EQ(result, expected);
}

TEST(st, futureContractInline) {
    {
        auto [f, p] = st::make_contract<std::monostate>();
        bool done = false;

        std::move(p).set_value();
        std::move(f).set_callback(st::inline_executor::instance(), [&done](std::monostate) noexcept { done = true; });

        ASSERT_TRUE(done);
    }
    {
        auto [f, p] = st::make_contract<std::monostate>();
        bool done = false;

        std::move(f).set_callback(st::inline_executor::instance(), [&done](std::monostate) noexcept { done = true; });
        ASSERT_FALSE(done);

        std::move(p).set_value();
        ASSERT_TRUE(done);
    }
}

TEST(st, futureContract) {
    st::executor executor;
    auto [f, p] = st::make_contract<std::monostate>();
    bool done = false;

    std::move(f).set_callback(executor, [&done](std::monostate) noexcept { done = true; });
    ASSERT_FALSE(done);
    std::move(p).set_value();
    ASSERT_FALSE(done);
    EXPECT_EQ(executor.execute_batch(), 1);
    ASSERT_TRUE(done);
}

TEST(st, futureAwait) {
    const auto task = [](bool& started, bool& ended, int& result, st::future<int> f) -> async<void> {
        started = true;
        std::cout << "start calculation" << std::endl;
        result = co_await std::move(f);
        std::cout << "calculation finished: " << result << std::endl;
        ended = true;
    };

    bool started = false;
    bool ended = false;
    int result = 0;
    auto [f, p] = st::make_contract<int>();
    auto coro = task(started, ended, result, std::move(f));

    ASSERT_FALSE(started);

    st::executor executor;
    schedule(executor, std::move(coro));

    EXPECT_EQ(executor.execute_batch(), 1);
    ASSERT_TRUE(started);
    ASSERT_FALSE(ended);

    EXPECT_EQ(executor.execute_batch(), 0);
    ASSERT_FALSE(ended);

    std::move(p).set_value(42);
    ASSERT_EQ(result, 0);
    ASSERT_FALSE(ended);

    EXPECT_EQ(executor.execute_batch(), 1);
    ASSERT_EQ(result, 42);
    ASSERT_TRUE(ended);
}

TEST(st, combinatorsFirst) {
    auto [f1, p1] = st::make_contract<int>();
    auto [f2, p2] = st::make_contract<int>();

    int result = 0;
    st::first(st::inline_executor::instance(), std::move(f1), std::move(f2))
        .set_callback(st::inline_executor::instance(), [&result](int x) noexcept { result = x; });
    ASSERT_EQ(result, 0);

    std::move(p2).set_value(42);
    ASSERT_EQ(result, 42);

    std::move(p1).set_value(13);
    ASSERT_EQ(result, 42);
}

TEST(st, combinatorsAll) {
    auto [f1, p1] = st::make_contract<int>();
    auto [f2, p2] = st::make_contract<std::string>();

    int result1 = 0;
    std::string result2{};
    st::all(st::inline_executor::instance(), std::move(f1), std::move(f2))
        .set_callback(st::inline_executor::instance(), [&result1, &result2](std::tuple<int, std::string> xy) noexcept {
            result1 = std::get<0>(xy);
            result2 = std::get<1>(xy);
        });
    ASSERT_EQ(result1, 0);
    ASSERT_EQ(result2, "");

    std::move(p1).set_value(42);
    ASSERT_EQ(result1, 0);
    ASSERT_EQ(result2, "");

    std::move(p2).set_value("the answer");
    ASSERT_EQ(result1, 42);
    ASSERT_EQ(result2, "the answer");
}

} // namespace sl::exec
