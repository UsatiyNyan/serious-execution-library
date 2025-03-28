//
// Created by usatiynyan.
//

#include "sl/exec/algo.hpp"
#include "sl/exec/model.hpp"
#include "sl/exec/thread/event.hpp"

#include <gtest/gtest.h>

namespace sl::exec {

TEST(conn, valueSignal) {
    const tl::optional<meta::result<int, meta::unit>> maybe_result = //
        as_signal(meta::result<int, meta::unit>(42)) //
        | get<nowait_event>();
    ASSERT_EQ(*maybe_result, 42);
}

TEST(algo, andThen) {
    const tl::optional<meta::result<std::string, meta::unit>> maybe_result =
        as_signal(meta::result<int, meta::unit>(42)) //
        | and_then([](int i) { return meta::result<int, meta::unit>{ i + 1 }; }) //
        | and_then([](int i) { return meta::result<std::string, meta::unit>{ std::to_string(i) }; }) //
        | get<nowait_event>();
    ASSERT_EQ(*maybe_result, "43");
}

TEST(algo, map) {
    const tl::optional<meta::result<std::string, meta::unit>> maybe_result = //
        as_signal(meta::result<int, meta::unit>(42)) //
        | map([](int i) { return i + 1; }) //
        | map([](int i) { return std::to_string(i); }) //
        | get<nowait_event>();
    ASSERT_EQ(*maybe_result, "43");
}

TEST(algo, mapError) {
    const tl::optional<meta::result<meta::unit, std::string>> maybe_result = //
        as_signal(meta::result<meta::unit, int>(meta::err(42))) //
        | map_error([](int i) { return i + 1; }) //
        | map_error([](int i) { return std::to_string(i); }) //
        | get<nowait_event>();
    ASSERT_EQ(*maybe_result, meta::err(std::string{ "43" }));
}

TEST(algo, orElse) {
    const tl::optional<meta::result<meta::unit, std::string>> maybe_result = //
        as_signal(meta::result<meta::unit, int>(meta::err(42))) //
        | or_else([](int i) { return meta::result<meta::unit, int>(meta::err(i + 1)); }) //
        | or_else([](int i) { return meta::result<meta::unit, std::string>(meta::err(std::to_string(i))); }) //
        | get<nowait_event>();
    ASSERT_EQ(*maybe_result, meta::err(std::string{ "43" }));
}

TEST(algo, manualStartOn) {
    manual_executor executor;

    bool done = false;
    start_on(executor) //
        | map([&done](meta::unit) {
              done = true;
              return meta::unit{};
          })
        | detach();

    ASSERT_FALSE(done);
    executor.execute_at_most(1);
    ASSERT_TRUE(done);
}

TEST(algo, manualSchedule) {
    manual_executor executor;

    bool done = false;
    schedule(executor, [&done] -> meta::result<meta::unit, meta::undefined> {
        done = true;
        return {};
    }) | detach();

    ASSERT_FALSE(done);
    executor.execute_at_most(1);
    ASSERT_TRUE(done);
}

TEST(algo, anySimple) {
    using result_type = meta::result<int, int>;
    auto s1 = as_signal(result_type{ tl::unexpect, 69 });
    auto s2 = as_signal(result_type{ 42 });
    auto s12 = any(std::move(s1), std::move(s2));
    auto maybe_result = std::move(s12) | get<nowait_event>();
    ASSERT_EQ(*maybe_result, 42);
}

TEST(algo, andSimple) {
    auto s1 = value_as_signal(42);
    auto s2 = value_as_signal(69);
    auto s12 = all(std::move(s1), std::move(s2));
    auto maybe_result = std::move(s12) | get<nowait_event>();
    ASSERT_EQ(*maybe_result, std::make_tuple(42, 69));
}

TEST(algo, forkSimple) {
    auto [l_signal, r_signal] = value_as_signal(42) | fork();
    auto l_value = std::move(l_signal) | get<nowait_event>();
    auto r_value = std::move(r_signal) | map([](int x) { return x + 27; }) | get<nowait_event>();
    EXPECT_EQ(*l_value, 42);
    EXPECT_EQ(*r_value, 69);
}

TEST(algo, subscribe) {
    manual_executor executor;

    int value = 0;
    subscribe_connection imalive = value_as_signal(42) //
                                   | continue_on(executor) //
                                   | map([&value](int x) {
                                         value = x;
                                         return meta::unit{};
                                     })
                                   | subscribe();
    std::move(imalive).emit();

    EXPECT_EQ(value, 0);
    EXPECT_EQ(executor.execute_at_most(1), 1);
    EXPECT_EQ(value, 42);
}

TEST(algo, force) {
    auto [future, promise] = exec::make_contract<int, meta::undefined>();
    promise.set_value(42);
    const int result = (std::move(future) | get<nowait_event>()).value().value();
    EXPECT_EQ(result, 42);
}

TEST(algo, forceMany) {
    using contract_type = contract<int, meta::undefined>;
    std::vector<contract_type::promise_type> promises;
    std::vector<int> results(10, 0);

    for (std::size_t i = 0; i != results.size(); ++i) {
        auto [future, promise] = exec::make_contract<int, meta::undefined>();
        std::move(future) | map([&results, i](int x) {
            results[i] = x;
            return meta::unit{};
        }) | detach();
        promises.push_back(std::move(promise));
    }

    for (auto& promise : promises) {
        promise.set_value(42);
    }

    EXPECT_EQ(std::vector<int>(10, 42), results);
}

TEST(algo, queryExecutor) {
    const auto maybe_inline_result = start_on(inline_executor()) //
                                     | query_executor()
                                     | map([](std::pair<executor&, meta::unit> p) -> executor* { return &p.first; })
                                     | get<nowait_event>();
    ASSERT_TRUE(maybe_inline_result.has_value());
    const auto& inline_result = maybe_inline_result.value();
    ASSERT_TRUE(inline_result.has_value());
    EXPECT_EQ(inline_result.value(), &inline_executor());

    manual_executor manual_executor;
    executor* manual_executor_ptr = nullptr;
    start_on(manual_executor) //
        | query_executor() //
        | map([&manual_executor_ptr](std::pair<executor&, meta::unit> p) {
              manual_executor_ptr = &p.first;
              return meta::unit{};
          })
        | detach();
    ASSERT_EQ(manual_executor_ptr, nullptr);
    manual_executor.execute_at_most(1);
    EXPECT_EQ(manual_executor_ptr, &manual_executor);
}

TEST(algo, flattenSchedule) {
    manual_executor executor1;
    manual_executor executor2;
    bool done1 = false;
    bool done2 = false;

    schedule(
        executor1,
        [&] {
            done1 = true;
            return meta::ok(schedule(executor2, [&] {
                done2 = true;
                return meta::ok(meta::unit{});
            }));
        }
    ) | flatten()
        | detach();

    ASSERT_FALSE(done1);
    ASSERT_FALSE(done2);

    EXPECT_EQ(executor2.execute_batch(), 0);
    EXPECT_FALSE(done1);
    EXPECT_FALSE(done2);

    EXPECT_EQ(executor1.execute_batch(), 1);
    EXPECT_TRUE(done1);
    EXPECT_FALSE(done2);

    EXPECT_EQ(executor1.execute_batch(), 0);
    EXPECT_TRUE(done1);
    EXPECT_FALSE(done2);

    EXPECT_EQ(executor2.execute_batch(), 1);
    EXPECT_TRUE(done1);
    EXPECT_TRUE(done2);

    EXPECT_EQ(executor1.execute_batch(), 0);
    EXPECT_EQ(executor2.execute_batch(), 0);
}

TEST(algo, flattenAndThen) {
    manual_executor executor1;
    manual_executor executor2;
    std::size_t counter1 = 0;
    bool done2 = false;

    start_on(executor1) //
        | and_then([&](meta::unit) {
              ++counter1;
              return meta::ok(
                  start_on(executor2) //
                  | map([&](meta::unit) {
                        done2 = true;
                        return meta::unit{};
                    })
              );
          })
        | flatten() //
        | and_then([&](meta::unit) {
              ++counter1; // continue execution on the "outer" executor
              return meta::ok(meta::unit{});
          })
        | detach();

    ASSERT_EQ(counter1, 0);
    ASSERT_FALSE(done2);

    EXPECT_EQ(executor2.execute_batch(), 0);
    EXPECT_EQ(counter1, 0);
    EXPECT_FALSE(done2);

    EXPECT_EQ(executor1.execute_batch(), 1);
    EXPECT_EQ(counter1, 1);
    EXPECT_FALSE(done2);

    EXPECT_EQ(executor1.execute_batch(), 0);
    EXPECT_EQ(counter1, 1);
    EXPECT_FALSE(done2);

    EXPECT_EQ(executor2.execute_batch(), 1);
    EXPECT_EQ(counter1, 1);
    EXPECT_TRUE(done2);

    EXPECT_EQ(executor1.execute_batch(), 1);
    EXPECT_EQ(counter1, 2);
    EXPECT_TRUE(done2);

    EXPECT_EQ(executor1.execute_batch(), 0);
    EXPECT_EQ(executor2.execute_batch(), 0);
}
} // namespace sl::exec
