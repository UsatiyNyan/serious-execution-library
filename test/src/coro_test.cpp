//
// Created by usatiynyan.
//

#include "sl/exec/algo.hpp"
#include "sl/exec/coro.hpp"

#include <gtest/gtest.h>

namespace sl::exec {

TEST(coro, coroutine) {
    constexpr auto calculate_meaning_of_life = [] -> coroutine<int> { co_return 42; };
    coroutine<int> meaning_of_life = calculate_meaning_of_life();
    std::cout << "calculating meaning of life brrr..." << std::endl;
    meaning_of_life.start();
    std::cout << "meaning of life calculated" << std::endl;
    const int meaning_of_life_result = std::move(meaning_of_life).result_or_throw();
    std::cout << "result: " << meaning_of_life_result << std::endl;
    ASSERT_EQ(meaning_of_life_result, 42);
}

coroutine<std::vector<std::string>> live_productive_day() {
    std::vector<std::string> done;
    std::cout << "ready for productive day!" << std::endl;

    constexpr auto shower = [] -> coroutine<std::string> {
        std::cout << "taking a shower" << std::endl;
        co_return "shower";
    };
    done.push_back(co_await shower());

    constexpr auto coffee = [] -> coroutine<std::string> {
        std::cout << "making some coffee" << std::endl;
        co_return "coffee";
    };
    done.push_back(co_await coffee());

    constexpr auto work = [] -> coroutine<std::vector<std::string>> {
        std::cout << "doing work" << std::endl;
        std::vector<std::string> work_done;

        constexpr auto jira = [] -> coroutine<std::string> { co_return "jira"; };
        work_done.push_back(co_await jira());

        constexpr auto coding = [] -> coroutine<std::string> { co_return "coding"; };
        work_done.push_back(co_await coding());

        constexpr auto git = [] -> coroutine<std::string> { co_return "git"; };
        work_done.push_back(co_await git());

        co_return work_done;
    };
    auto work_result = co_await work();
    done.insert(done.end(), work_result.begin(), work_result.end());

    constexpr auto eat = [] -> coroutine<std::string> {
        std::cout << "eating, yummy" << std::endl;
        co_return "eat";
    };
    done.push_back(co_await eat());

    std::cout << "day ended, time to sleep Z-z-z..." << std::endl;
    co_return done;
}

TEST(coro, nestingTask) {
    auto productive_day = live_productive_day();
    productive_day.start();
    const auto productive_day_result = std::move(productive_day).result_or_throw();
    ASSERT_EQ(
        productive_day_result,
        (std::vector<std::string>{
            "shower",
            "coffee",
            "jira",
            "coding",
            "git",
            "eat",
        })
    );
}

TEST(coro, coroutinesCallStack) {
    constexpr int iterations = 10'000'000;
    constexpr auto synchronous = [] -> coroutine<int> { co_return 1; };
    constexpr auto iter_synchronous = [synchronous] -> coroutine<int> {
        int sum = 0;
        for (int i = 0; i < iterations; ++i) {
            sum += co_await synchronous();
        }
        co_return sum;
    };
    auto iter_synchronous_coroutine = iter_synchronous();
    iter_synchronous_coroutine.start();
    const int result = std::move(iter_synchronous_coroutine).result_or_throw();
    ASSERT_EQ(result, iterations);
}

TEST(coro, generator) {
    auto create_iota = [](int i) -> generator<int> {
        while (true) {
            co_yield i;
            ++i;
        }
    };

    auto iota = create_iota(10);
    const auto maybe_10 = iota.next_or_throw();
    ASSERT_TRUE(maybe_10.has_value());
    EXPECT_EQ(maybe_10.value(), 10);
    const auto maybe_11 = iota.next_or_throw();
    ASSERT_TRUE(maybe_11.has_value());
    EXPECT_EQ(maybe_11.value(), 11);
    const auto maybe_12 = iota.next_or_throw();
    ASSERT_TRUE(maybe_12.has_value());
    EXPECT_EQ(maybe_12.value(), 12);
    const auto maybe_13 = iota.next_or_throw();
    ASSERT_TRUE(maybe_13.has_value());
    EXPECT_EQ(maybe_13.value(), 13);
    const auto maybe_14 = iota.next_or_throw();
    ASSERT_TRUE(maybe_14.has_value());
    EXPECT_EQ(maybe_14.value(), 14);
}

TEST(coro, generator_iterator) {
    auto create_iota = [](int begin, int end) -> generator<int> {
        for (int i = begin; i < end; ++i) {
            co_yield i;
        }
    };

    const int begin = 1;
    const int end = 15;
    int counter = begin;
    for (const int iota : create_iota(begin, end)) {
        EXPECT_EQ(counter, iota);
        ++counter;
    }
    EXPECT_EQ(counter, end);
}

generator<int> create_nesting_iota(int begin, int end, int level = 0) {
    if (begin >= end) {
        co_return;
    }

    const int half = begin + (end - begin) / 2;

    {
        auto l = create_nesting_iota(begin, half, level + 1);
        while (auto l_value = l.next_or_throw()) {
            co_yield std::move(l_value).value();
        }
    }

    co_yield half;

    {
        auto r = create_nesting_iota(half + 1, end, level + 1);
        while (auto r_value = r.next_or_throw()) {
            co_yield std::move(r_value).value();
        }
    }
};

TEST(coro, nestingGenerator) {
    auto nesting_iota = create_nesting_iota(0, 10);
    for (int i = 0; i < 10; ++i) {
        const auto maybe_value = nesting_iota.next_or_throw();
        ASSERT_TRUE(maybe_value.has_value());
        EXPECT_EQ(maybe_value.value(), i);
    }
    // should finish at 10th iter
    ASSERT_FALSE(nesting_iota.next_or_throw().has_value());
}

TEST(coro, generatorReturn) {
    auto create_iota = [](int begin, int end) -> generator<int, std::string> {
        for (int i = begin; i < end; ++i) {
            co_yield i;
        }
        co_return "done";
    };

    auto iota = create_iota(1, 15);
    for (const int _ [[maybe_unused]] : iota) {}
    ASSERT_EQ(std::move(iota).result(), "done");
}

TEST(coro, asyncOne) {
    manual_executor executor;
    std::size_t counter = 0;

    auto coro = [&counter] -> async<void> {
        ++counter;
        co_return;
    };
    coro_schedule(executor, coro());
    ASSERT_EQ(counter, 0);

    EXPECT_EQ(executor.execute_batch(), 1);
    ASSERT_EQ(counter, 1);
}

TEST(coro, asyncMany) {
    manual_executor executor;
    std::size_t counter = 0;
    constexpr std::size_t expected = 1000;

    auto inner = [] -> async<std::size_t> { co_return 1; };
    auto outer = [&counter, inner] -> async<void> {
        for (std::size_t i = 0; i < expected; ++i) {
            counter += co_await inner();
        }
        co_return;
    };
    coro_schedule(executor, outer());
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

TEST(coro, asyncNesting) {
    manual_executor executor;
    constexpr std::size_t expected = 1'000'000;
    std::size_t result = 0;

    coro_schedule(executor, [&result] -> async<void> { result = co_await nesting_coro(expected); }());
    ASSERT_EQ(result, 0);

    EXPECT_EQ(executor.execute_batch(), 1);
    ASSERT_EQ(result, expected);
}

TEST(coro, asyncGenOne) {
    manual_executor executor;
    std::size_t counter = 0;

    constexpr auto inner_coro = [] -> async<std::size_t> { co_return 1u; };
    constexpr auto gen_coro = [inner_coro] -> async_gen<std::size_t, std::string> {
        const auto value = co_await inner_coro();
        co_yield value;
        co_return "done";
    };
    auto coro = [&counter, gen_coro] -> async<void> {
        auto g = gen_coro();
        auto value = co_await g;
        ASSERT(value.has_value());
        counter = value.value();

        auto nothing = co_await g;
        ASSERT(!nothing.has_value());

        ASSERT(std::move(g).result() == "done");
        co_return;
    };
    coro_schedule(executor, coro());

    EXPECT_EQ(executor.execute_batch(), 1);
    ASSERT_EQ(counter, 1);
}

TEST(coro, asyncGenMany) {
    manual_executor executor;
    std::size_t counter = 0;
    constexpr std::size_t expected = 1000;

    constexpr auto inner_coro = [] -> async<std::size_t> { co_return 1u; };
    constexpr auto gen_coro = [inner_coro] -> async_gen<std::size_t> {
        for (std::size_t i = 0; i < expected; ++i) {
            const std::size_t value = co_await inner_coro();
            co_yield value;
        }
    };
    auto coro = [&counter, gen_coro] -> async<void> {
        auto g = gen_coro();
        while (auto maybe_value = co_await g) {
            counter += maybe_value.value();
        }
        co_return;
    };
    coro_schedule(executor, coro());

    EXPECT_EQ(executor.execute_batch(), 1);
    ASSERT_EQ(counter, expected);
}

TEST(coro, asyncGenNesting) {
    manual_executor executor;
    std::size_t counter = 0;
    constexpr std::size_t expected = 1000;

    constexpr auto inner_coro = [] -> async<std::size_t> { co_return 1u; };
    constexpr auto inner_gen_coro = [inner_coro] -> async_gen<std::size_t> {
        for (std::size_t i = 0; i < expected; ++i) {
            const std::size_t value = co_await inner_coro();
            co_yield value;
        }
    };
    constexpr auto gen_coro = [inner_coro, inner_gen_coro] -> async_gen<std::size_t> {
        auto g = inner_gen_coro();
        while (auto maybe_value = co_await g) {
            const std::size_t other_value = co_await inner_coro();
            co_yield maybe_value.value() + other_value;
        }
        co_return;
    };
    auto coro = [&counter, gen_coro] -> async<void> {
        auto g = gen_coro();
        while (auto maybe_value = co_await g) {
            counter += maybe_value.value();
        }
        co_return;
    };
    coro_schedule(executor, coro());

    EXPECT_EQ(executor.execute_batch(), 1);
    ASSERT_EQ(counter, expected * 2);
}

TEST(coro, awaitSignal) {
    manual_executor executor;
    std::size_t counter = 0;

    auto coro = [&counter] -> async<void> {
        auto result = co_await as_signal(meta::result<std::size_t, meta::undefined>{ 42 });
        counter += result.value();
    };
    coro_schedule(executor, coro());
    ASSERT_EQ(counter, 0);

    EXPECT_EQ(executor.execute_batch(), 1);
    ASSERT_EQ(counter, 42);
}

TEST(coro, awaitExecutor) {
    manual_executor executor1;
    manual_executor executor2;

    int counter1 = 0;
    int counter2 = 0;

    auto coro = [&] -> async<void> {
        ++counter1;
        co_await schedule(executor2);
        ++counter2;
    };
    coro_schedule(executor1, coro());

    EXPECT_EQ(executor1.execute_batch(), 1);
    ASSERT_EQ(counter1, 1);
    ASSERT_EQ(counter2, 0);

    EXPECT_EQ(executor1.execute_batch(), 0);
    ASSERT_EQ(counter1, 1);
    ASSERT_EQ(counter2, 0);

    EXPECT_EQ(executor2.execute_batch(), 1);
    ASSERT_EQ(counter1, 1);
    ASSERT_EQ(counter2, 1);
}

} // namespace sl::exec
