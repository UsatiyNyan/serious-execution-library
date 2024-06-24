//
// Created by usatiynyan.
//

#include "sl/exec/coro.hpp"

#include <gtest/gtest.h>

namespace sl::exec {

generator<int> create_iota(int i) {
    while (true) {
        co_yield i;
        ++i;
    }
};

TEST(coro, generator) {
    auto iota = create_iota(10);
    const auto maybe_10 = iota.next();
    ASSERT_TRUE(maybe_10.has_value());
    EXPECT_EQ(maybe_10.value(), 10);
    const auto maybe_11 = iota.next();
    ASSERT_TRUE(maybe_11.has_value());
    EXPECT_EQ(maybe_11.value(), 11);
    const auto maybe_12 = iota.next();
    ASSERT_TRUE(maybe_12.has_value());
    EXPECT_EQ(maybe_12.value(), 12);
    const auto maybe_13 = iota.next();
    ASSERT_TRUE(maybe_13.has_value());
    EXPECT_EQ(maybe_13.value(), 13);
    const auto maybe_14 = iota.next();
    ASSERT_TRUE(maybe_14.has_value());
    EXPECT_EQ(maybe_14.value(), 14);
}

generator<int> create_nesting_iota(int begin, int end, int level = 0) {
    if (begin >= end) {
        co_return;
    }

    const int half = begin + (end - begin) / 2;

    {
        auto l = create_nesting_iota(begin, half, level + 1);
        while (auto l_value = l.next()) {
            co_yield std::move(l_value).value();
        }
    }

    co_yield half;

    {
        auto r = create_nesting_iota(half + 1, end, level + 1);
        while (auto r_value = r.next()) {
            co_yield std::move(r_value).value();
        }
    }
};

TEST(coro, nestingGenerator) {
    auto nesting_iota = create_nesting_iota(0, 10);
    for (int i = 0; i < 10; ++i) {
        const auto maybe_value = nesting_iota.next();
        ASSERT_TRUE(maybe_value.has_value());
        EXPECT_EQ(maybe_value.value(), i);
    }
    // should finish at 10th iter
    ASSERT_FALSE(nesting_iota.next().has_value());
}

task<int> calculate_meaning_of_life() {
    std::cout << "calculating meaning of life brrr..." << std::endl;
    co_return 42;
}

TEST(coro, task) {
    task<int> meaning_of_life = calculate_meaning_of_life();
    std::cout << "meaning of life calculated" << std::endl;
    const int meaning_of_life_result = std::move(meaning_of_life).result();
    std::cout << "result: " << meaning_of_life_result << std::endl;
    ASSERT_EQ(meaning_of_life_result, 42);
}

task<std::vector<std::string>> live_productive_day() {
    std::vector<std::string> done;
    std::cout << "ready for productive day!" << std::endl;

    constexpr auto shower = [] -> task<std::string> {
        std::cout << "taking a shower" << std::endl;
        co_return "shower";
    };
    done.push_back(co_await shower());

    constexpr auto coffee = [] -> task<std::string> {
        std::cout << "making some coffee" << std::endl;
        co_return "coffee";
    };
    done.push_back(co_await coffee());

    constexpr auto work = [] -> task<std::vector<std::string>> {
        std::cout << "doing work" << std::endl;
        std::vector<std::string> work_done;

        constexpr auto jira = [] -> task<std::string> { co_return "jira"; };
        work_done.push_back(co_await jira());

        constexpr auto coding = [] -> task<std::string> { co_return "coding"; };
        work_done.push_back(co_await coding());

        constexpr auto git = [] -> task<std::string> { co_return "git"; };
        work_done.push_back(co_await git());

        co_return work_done;
    };
    auto work_result = co_await work();
    done.insert(done.end(), work_result.begin(), work_result.end());

    constexpr auto eat = [] -> task<std::string> {
        std::cout << "eating, yummy" << std::endl;
        co_return "eat";
    };
    done.push_back(co_await eat());

    std::cout << "day ended, time to sleep Z-z-z..." << std::endl;
    co_return done;
}

TEST(coro, nestingTask) {
    auto productive_day = live_productive_day();
    const auto productive_day_result = std::move(productive_day).result();
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

} // namespace sl::exec
