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

TEST(coro, iota) {
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

TEST(coro, iotaNesting) {
    auto nesting_iota = create_nesting_iota(0, 10);
    for (int i = 0; i < 10; ++i) {
        const auto maybe_value = nesting_iota.next();
        ASSERT_TRUE(maybe_value.has_value());
        EXPECT_EQ(maybe_value.value(), i);
    }
    // should finish at 10th iter
    ASSERT_FALSE(nesting_iota.next().has_value());
}

} // namespace sl::exec
