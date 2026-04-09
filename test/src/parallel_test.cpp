//
// Created by usatiynyan.
// Tests for parallel signal combinators
//

#include "sl/exec/algo.hpp"
#include "sl/exec/algo/sched/manual.hpp"
#include "sl/exec/model.hpp"
#include "sl/exec/thread/event/nowait.hpp"

#include <gtest/gtest.h>

namespace sl::exec {

// ============================================================================
// any() tests - first value wins, if all fail then last failure reported
// ============================================================================

TEST(parallel, anyBothValues) {
    // First value wins
    using result_type = meta::result<int, meta::unit>;
    auto s1 = as_signal(result_type{ 42 });
    auto s2 = as_signal(result_type{ 69 });
    auto maybe_result = any(std::move(s1), std::move(s2)) | get<nowait_event>();
    ASSERT_TRUE(maybe_result.has_value());
    ASSERT_TRUE(maybe_result->has_value());
    EXPECT_EQ(maybe_result->value(), 42);
}

TEST(parallel, anyValueThenNull) {
    // Value wins over null
    using result_type = meta::result<int, meta::unit>;
    auto s1 = as_signal(result_type{ 42 });
    auto s2 = null_as_signal<int, meta::unit>();
    auto maybe_result = any(std::move(s1), std::move(s2)) | get<nowait_event>();
    ASSERT_TRUE(maybe_result.has_value());
    ASSERT_TRUE(maybe_result->has_value());
    EXPECT_EQ(maybe_result->value(), 42);
}

TEST(parallel, anyNullThenValue) {
    // Value wins even if null comes first
    using result_type = meta::result<int, meta::unit>;
    auto s1 = null_as_signal<int, meta::unit>();
    auto s2 = as_signal(result_type{ 69 });
    auto maybe_result = any(std::move(s1), std::move(s2)) | get<nowait_event>();
    ASSERT_TRUE(maybe_result.has_value());
    ASSERT_TRUE(maybe_result->has_value());
    EXPECT_EQ(maybe_result->value(), 69);
}

TEST(parallel, anyBothNull) {
    // Both null - result is null
    auto s1 = null_as_signal<int, meta::unit>();
    auto s2 = null_as_signal<int, meta::unit>();
    auto maybe_result = any(std::move(s1), std::move(s2)) | get<nowait_event>();
    ASSERT_FALSE(maybe_result.has_value());
}

TEST(parallel, anyBothError) {
    // Both error - last error reported
    using result_type = meta::result<int, std::string>;
    auto s1 = as_signal(result_type{ tl::unexpect, "error1" });
    auto s2 = as_signal(result_type{ tl::unexpect, "error2" });
    auto maybe_result = any(std::move(s1), std::move(s2)) | get<nowait_event>();
    ASSERT_TRUE(maybe_result.has_value());
    ASSERT_FALSE(maybe_result->has_value());
    EXPECT_EQ(maybe_result->error(), "error2");
}

TEST(parallel, anyErrorThenValue) {
    // Value wins over error
    using result_type = meta::result<int, std::string>;
    auto s1 = as_signal(result_type{ tl::unexpect, "error" });
    auto s2 = as_signal(result_type{ 42 });
    auto maybe_result = any(std::move(s1), std::move(s2)) | get<nowait_event>();
    ASSERT_TRUE(maybe_result.has_value());
    ASSERT_TRUE(maybe_result->has_value());
    EXPECT_EQ(maybe_result->value(), 42);
}

TEST(parallel, anyValueThenError) {
    // Value wins over error
    using result_type = meta::result<int, std::string>;
    auto s1 = as_signal(result_type{ 42 });
    auto s2 = as_signal(result_type{ tl::unexpect, "error" });
    auto maybe_result = any(std::move(s1), std::move(s2)) | get<nowait_event>();
    ASSERT_TRUE(maybe_result.has_value());
    ASSERT_TRUE(maybe_result->has_value());
    EXPECT_EQ(maybe_result->value(), 42);
}

TEST(parallel, anyThreeSignals) {
    // Three signals - first value wins
    using result_type = meta::result<int, meta::unit>;
    auto s1 = as_signal(result_type{ tl::unexpect, meta::unit{} });
    auto s2 = as_signal(result_type{ 42 });
    auto s3 = as_signal(result_type{ 69 });
    auto maybe_result = any(std::move(s1), std::move(s2), std::move(s3)) | get<nowait_event>();
    ASSERT_TRUE(maybe_result.has_value());
    ASSERT_TRUE(maybe_result->has_value());
    EXPECT_EQ(maybe_result->value(), 42);
}

TEST(parallel, anyWithManualExecutor) {
    // Test async behavior with manual executor
    manual_executor executor;
    using result_type = meta::result<int, meta::unit>;

    int result_value = 0;
    auto s1 = as_signal(result_type{ tl::unexpect, meta::unit{} });
    auto s2 = schedule(executor, [] { return result_type{ 42 }; });

    any(std::move(s1), std::move(s2))
        | map([&result_value](int x) {
              result_value = x;
              return meta::unit{};
          })
        | detach();

    EXPECT_EQ(result_value, 0);
    executor.execute_batch();
    EXPECT_EQ(result_value, 42);
}

TEST(parallel, anyWithManualExecutorBothAsync) {
    // Both signals async - first to complete wins
    manual_executor executor1;
    manual_executor executor2;
    using result_type = meta::result<int, meta::unit>;

    int result_value = 0;
    auto s1 = schedule(executor1, [] { return result_type{ 42 }; });
    auto s2 = schedule(executor2, [] { return result_type{ 69 }; });

    any(std::move(s1), std::move(s2))
        | map([&result_value](int x) {
              result_value = x;
              return meta::unit{};
          })
        | detach();

    EXPECT_EQ(result_value, 0);

    // Execute s2 first - it should win
    executor2.execute_batch();
    EXPECT_EQ(result_value, 69);

    // Execute s1 - should be ignored (already have a winner)
    executor1.execute_batch();
    EXPECT_EQ(result_value, 69);
}

// ============================================================================
// all() tests - all values required, first failure wins
// ============================================================================

TEST(parallel, allBothValues) {
    // Both values - success with tuple
    auto s1 = value_as_signal(42);
    auto s2 = value_as_signal(std::string{ "hello" });
    auto maybe_result = all(std::move(s1), std::move(s2)) | get<nowait_event>();
    ASSERT_TRUE(maybe_result.has_value());
    ASSERT_TRUE(maybe_result->has_value());
    EXPECT_EQ(maybe_result->value(), std::make_tuple(42, std::string{ "hello" }));
}

TEST(parallel, allValueThenError) {
    // Error wins, cancels pending
    using result_type = meta::result<int, std::string>;
    auto s1 = as_signal(result_type{ 42 });
    auto s2 = as_signal(result_type{ tl::unexpect, "error" });
    auto maybe_result = all(std::move(s1), std::move(s2)) | get<nowait_event>();
    ASSERT_TRUE(maybe_result.has_value());
    ASSERT_FALSE(maybe_result->has_value());
    EXPECT_EQ(maybe_result->error(), "error");
}

TEST(parallel, allErrorThenValue) {
    // First error wins
    using result_type = meta::result<int, std::string>;
    auto s1 = as_signal(result_type{ tl::unexpect, "error" });
    auto s2 = as_signal(result_type{ 42 });
    auto maybe_result = all(std::move(s1), std::move(s2)) | get<nowait_event>();
    ASSERT_TRUE(maybe_result.has_value());
    ASSERT_FALSE(maybe_result->has_value());
    EXPECT_EQ(maybe_result->error(), "error");
}

TEST(parallel, allBothError) {
    // First error wins
    using result_type = meta::result<int, std::string>;
    auto s1 = as_signal(result_type{ tl::unexpect, "error1" });
    auto s2 = as_signal(result_type{ tl::unexpect, "error2" });
    auto maybe_result = all(std::move(s1), std::move(s2)) | get<nowait_event>();
    ASSERT_TRUE(maybe_result.has_value());
    ASSERT_FALSE(maybe_result->has_value());
    EXPECT_EQ(maybe_result->error(), "error1");
}

TEST(parallel, allValueThenNull) {
    // Null wins over pending values
    using result_type = meta::result<int, meta::unit>;
    auto s1 = as_signal(result_type{ 42 });
    auto s2 = null_as_signal<int, meta::unit>();
    auto maybe_result = all(std::move(s1), std::move(s2)) | get<nowait_event>();
    ASSERT_FALSE(maybe_result.has_value());
}

TEST(parallel, allNullThenValue) {
    // First null wins
    using result_type = meta::result<int, meta::unit>;
    auto s1 = null_as_signal<int, meta::unit>();
    auto s2 = as_signal(result_type{ 42 });
    auto maybe_result = all(std::move(s1), std::move(s2)) | get<nowait_event>();
    ASSERT_FALSE(maybe_result.has_value());
}

TEST(parallel, allThreeSignals) {
    // Three signals - all must succeed
    auto s1 = value_as_signal(1);
    auto s2 = value_as_signal(2);
    auto s3 = value_as_signal(3);
    auto maybe_result = all(std::move(s1), std::move(s2), std::move(s3)) | get<nowait_event>();
    ASSERT_TRUE(maybe_result.has_value());
    ASSERT_TRUE(maybe_result->has_value());
    EXPECT_EQ(maybe_result->value(), std::make_tuple(1, 2, 3));
}

TEST(parallel, allThreeSignalsOneError) {
    // Three signals - one error fails all
    using result_type = meta::result<int, std::string>;
    auto s1 = as_signal(result_type{ 1 });
    auto s2 = as_signal(result_type{ tl::unexpect, "error" });
    auto s3 = as_signal(result_type{ 3 });
    auto maybe_result = all(std::move(s1), std::move(s2), std::move(s3)) | get<nowait_event>();
    ASSERT_TRUE(maybe_result.has_value());
    ASSERT_FALSE(maybe_result->has_value());
    EXPECT_EQ(maybe_result->error(), "error");
}

TEST(parallel, allWithManualExecutor) {
    // Test async behavior with manual executor
    manual_executor executor;
    using result_type = meta::result<int, meta::unit>;

    std::tuple<int, int> result_value{};
    auto s1 = as_signal(result_type{ 42 });
    auto s2 = schedule(executor, [] { return result_type{ 69 }; });

    all(std::move(s1), std::move(s2))
        | map([&result_value](std::tuple<int, int> x) {
              result_value = x;
              return meta::unit{};
          })
        | detach();

    EXPECT_EQ(result_value, std::make_tuple(0, 0));
    executor.execute_batch();
    EXPECT_EQ(result_value, std::make_tuple(42, 69));
}

TEST(parallel, allWithManualExecutorBothAsync) {
    // Both signals async - must wait for both
    manual_executor executor1;
    manual_executor executor2;
    using result_type = meta::result<int, meta::unit>;

    std::tuple<int, int> result_value{};
    auto s1 = schedule(executor1, [] { return result_type{ 42 }; });
    auto s2 = schedule(executor2, [] { return result_type{ 69 }; });

    all(std::move(s1), std::move(s2))
        | map([&result_value](std::tuple<int, int> x) {
              result_value = x;
              return meta::unit{};
          })
        | detach();

    EXPECT_EQ(result_value, std::make_tuple(0, 0));

    // Execute s1 - not complete yet (waiting for s2)
    executor1.execute_batch();
    EXPECT_EQ(result_value, std::make_tuple(0, 0));

    // Execute s2 - now both complete
    executor2.execute_batch();
    EXPECT_EQ(result_value, std::make_tuple(42, 69));
}

TEST(parallel, allWithManualExecutorErrorCancels) {
    // Error should cancel pending signals
    manual_executor executor1;
    manual_executor executor2;
    using result_type = meta::result<int, std::string>;

    std::string error_value;
    auto s1 = schedule(executor1, [] { return result_type{ tl::unexpect, "error" }; });
    auto s2 = schedule(executor2, [] { return result_type{ 42 }; });

    all(std::move(s1), std::move(s2))
        | map_error([&error_value](std::string e) {
              error_value = e;
              return e;
          })
        | detach();

    EXPECT_EQ(error_value, "");

    // Execute s1 (error) - should report error and cancel s2
    executor1.execute_batch();
    EXPECT_EQ(error_value, "error");

    // Execute s2 - should be cancelled/ignored
    executor2.execute_batch();
    EXPECT_EQ(error_value, "error");
}

TEST(parallel, allHeterogeneousTypes) {
    // Different value types in tuple
    auto s1 = value_as_signal(42);
    auto s2 = value_as_signal(std::string{ "hello" });
    auto s3 = value_as_signal(3.14);
    auto maybe_result = all(std::move(s1), std::move(s2), std::move(s3)) | get<nowait_event>();
    ASSERT_TRUE(maybe_result.has_value());
    ASSERT_TRUE(maybe_result->has_value());
    auto [i, s, d] = maybe_result->value();
    EXPECT_EQ(i, 42);
    EXPECT_EQ(s, "hello");
    EXPECT_DOUBLE_EQ(d, 3.14);
}

// ============================================================================
// select tests - verify proper cleanup
// ============================================================================

TEST(parallel, selectSingleImmediate) {
    // Simplest case: select with just one immediate signal
    using result_type = meta::result<int, meta::unit>;
    int result = -1;

    select()
        .case_(
            as_signal(result_type{ 42 }),
            [&](int x) {
                result = x;
                return 0;
            }
        )
        | detach();

    EXPECT_EQ(result, 42);
}

TEST(parallel, selectTwoImmediates) {
    // Select with two immediate signals - first wins
    using result_type = meta::result<int, meta::unit>;
    int result = -1;

    select()
        .case_(
            as_signal(result_type{ 42 }),
            [&](int x) {
                result = x;
                return 0;
            }
        )
        .case_(
            as_signal(result_type{ 69 }),
            [&](int x) {
                result = x;
                return 1;
            }
        )
        | detach();

    EXPECT_EQ(result, 42);
}

TEST(parallel, selectImmediateAndChannelRecv) {
    // Scenario: select with immediate signal + channel receive
    // When immediate signal wins, the channel receive should be properly cancelled
    // Uses inline serial executor (like the working selectChannel test)

    auto channel = make_channel<int>();

    int result = -1;
    bool select_completed = false;

    // Create select: immediate value wins, channel receive should be cancelled
    // Note: select requires error_type = meta::unit
    using result_type = meta::result<int, meta::unit>;
    select()
        .case_(
            as_signal(result_type{ 42 }),
            [&](int x) {
                result = x;
                select_completed = true;
                return 0;
            }
        )
        .case_(
            channel->receive(),
            [&](int x) {
                result = x;
                select_completed = true;
                return 1;
            }
        )
        | detach();

    // Immediate signal completes synchronously - result should be set
    // With inline executor, try_cancel runs immediately
    EXPECT_EQ(result, 42);
    EXPECT_TRUE(select_completed);

    // Close channel to clean up
    channel->close() | get<nowait_event>();
}

TEST(parallel, selectImmediateAndChannelSend) {
    // Symmetric case: select with immediate signal + channel send
    // When immediate signal wins, the channel send should be properly cancelled

    auto channel = make_channel<int>();

    int result = -1;

    // Create select: immediate value wins, channel send should be cancelled
    // Note: select requires error_type = meta::unit
    using result_type = meta::result<int, meta::unit>;
    select()
        .case_(
            as_signal(result_type{ 42 }),
            [&](int x) {
                result = x;
                return 0;
            }
        )
        .case_(
            channel->send(100),
            [&](meta::unit) {
                result = 100;
                return 1;
            }
        )
        | detach();

    // Immediate signal wins
    EXPECT_EQ(result, 42);

    // Close channel to clean up the pending send
    channel->close() | get<nowait_event>();
}

TEST(parallel, selectTwoChannelReceives) {
    // Test select with two channel receives
    // Similar to selectChannel but simpler

    auto channel1 = make_channel<int>();
    auto channel2 = make_channel<std::string>();

    int result = -1;

    // Create select with two channel receives
    select()
        .case_(
            channel1->receive(),
            [&](int x) {
                result = x;
                return 0;
            }
        )
        .case_(
            channel2->receive(),
            [&](std::string) {
                result = 1;
                return 1;
            }
        )
        | detach();

    // Both receives are queued, select not done yet
    EXPECT_EQ(result, -1);

    // Send to channel1 - this will complete case 1, cancel case 2
    channel1->send(42) | get<nowait_event>();
    EXPECT_EQ(result, 42);

    // Close channels to clean up
    channel1->close() | get<nowait_event>();
    channel2->close() | get<nowait_event>();
}

// Test to isolate the leak issue - just immediate signals, no channel
// Test: select with mixed signal types (ordered and non-ordered)
// Verifies that emit_ordered correctly handles cancellation when
// connections are reordered by ordering value
TEST(parallel, selectMixedOrderingRepeated) {
    // Run multiple times to verify no memory leaks
    for (int i = 0; i < 10; ++i) {
        auto channel = make_channel<int>();
        int result = -1;

        // Immediate signal (non-ordered) + channel send (ordered)
        // Immediate wins, channel send should be properly cancelled
        using result_type = meta::result<int, meta::unit>;
        select()
            .case_(
                as_signal(result_type{ 42 }),
                [&](int x) {
                    result = x;
                    return 0;
                }
            )
            .case_(
                channel->send(100),
                [&](meta::unit) {
                    result = 100;
                    return 1;
                }
            )
            | detach();

        EXPECT_EQ(result, 42);
        channel->close() | get<nowait_event>();
    }
}

} // namespace sl::exec
