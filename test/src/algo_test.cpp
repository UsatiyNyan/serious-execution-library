//
// Created by usatiynyan.
//

#include "sl/exec/algo.hpp"
#include "sl/exec/algo/sched/manual.hpp"
#include "sl/exec/model.hpp"
#include "sl/exec/thread/event.hpp"
#include "sl/exec/thread/event/nowait.hpp"

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

TEST(algo, anySimple) {
    using result_type = meta::result<int, int>;
    auto s1 = as_signal(result_type{ tl::unexpect, 69 });
    auto s2 = as_signal(result_type{ 42 });
    auto s12 = any(std::move(s1), std::move(s2));
    auto maybe_result = std::move(s12) | get<nowait_event>();
    ASSERT_EQ(*maybe_result, 42);
}

TEST(algo, allSimple) {
    auto s1 = value_as_signal(42);
    auto s2 = value_as_signal(69);
    auto s12 = all(std::move(s1), std::move(s2));
    auto maybe_result = std::move(s12) | get<nowait_event>();
    ASSERT_EQ(*maybe_result, std::make_tuple(42, 69));
}

TEST(algo, share) {
    manual_executor executor;

    bool done1 = false;
    auto signal1 = schedule(
                       executor,
                       [&done1] {
                           done1 = true;
                           return meta::ok(42);
                       }
                   )
                   | continue_on(executor); // this continue should not affect
    meta::maybe<share_box<int, meta::undefined>> maybe_shared{ std::move(signal1) | share() };
    auto& shared = maybe_shared.value();

    // check lazyness
    EXPECT_EQ(executor.execute_batch(), 0);
    EXPECT_FALSE(done1);

    int result2 = 0;
    auto signal2 = shared.get_signal() | map([&result2](int x) {
                       result2 = x;
                       return meta::unit{};
                   });

    // check lazyness
    EXPECT_EQ(executor.execute_batch(), 0);
    EXPECT_FALSE(done1);
    EXPECT_EQ(result2, 0);

    std::move(signal2) | detach();

    // check fulfillment
    EXPECT_EQ(executor.execute_batch(), 1);
    EXPECT_TRUE(done1);
    EXPECT_EQ(result2, 42);

    // check eagerness
    const auto maybe_result3 = shared.get_signal() | map([](int x) { return x + 27; }) | get<nowait_event>();

    EXPECT_EQ(executor.execute_batch(), 0);
    ASSERT_TRUE(maybe_result3.has_value());
    ASSERT_TRUE(maybe_result3->has_value());
    EXPECT_EQ(maybe_result3->value(), 69);

    // check refcount
    auto signal4 = shared.get_signal();
    auto signal5 = shared.get_signal(); // valgrind will fail here
    maybe_shared.reset();
    const auto maybe_result4 = std::move(signal4) | get<nowait_event>();
    EXPECT_EQ(executor.execute_batch(), 0);
    ASSERT_TRUE(maybe_result4.has_value());
    ASSERT_TRUE(maybe_result4->has_value());
    EXPECT_EQ(maybe_result4->value(), 42);
}

TEST(algo, forkSimple) {
    auto [l_signal, r_signal] = value_as_signal(42) | fork();
    auto l_value = std::move(l_signal) | get<nowait_event>();
    auto r_value = std::move(r_signal) | map([](int x) { return x + 27; }) | get<nowait_event>();
    EXPECT_EQ(*l_value, 42);
    EXPECT_EQ(*r_value, 69);
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

TEST(algo, box) {
    auto signal = value_as_signal(42) | and_then([](int x) { return meta::ok(x + 27); });
    box_signal<int, meta::undefined> boxed_signal = std::move(signal) | box();
    const auto maybe_result = std::move(boxed_signal) | get<nowait_event>();
    ASSERT_TRUE(maybe_result.has_value());
    EXPECT_EQ(maybe_result.value(), 69);
}

TEST(algo, pipeSimple) {
    auto [in, out] = make_pipe<meta::unit, meta::undefined>();

    std::size_t counter = 0;
    const auto increment = [&](meta::unit) {
        ++counter;
        return meta::unit{};
    };

    in.send(meta::unit{}) | detach();
    // would ASSERT with "only single producer"
    // in.send(meta::unit{}) | detach();

    out.receive() | map(increment) | get<nowait_event>();
    ASSERT_EQ(counter, 1);

    out.receive() | map(increment) | detach();
    // would ASSERT with "only single consumer"
    // out.receive() | map(increment) | detach();

    EXPECT_EQ(counter, 1);

    for (int i = 2; i < 10; ++i) {
        in.send(meta::unit{}) | get<nowait_event>();
        EXPECT_EQ(counter, i);

        out.receive() | map(increment) | detach();
    }

    std::move(in).close() | get<nowait_event>();
    EXPECT_EQ(counter, 9);
}

TEST(algo, pipeExecutor) {
    auto [in, out] = make_pipe<meta::unit, meta::undefined>();

    std::size_t counter = 0;
    const auto increment = [&](meta::unit) {
        ++counter;
        return meta::unit{};
    };

    manual_executor an_executor;
    const auto increment_on_executor = [&](auto&& signal) {
        return std::move(signal) | continue_on(an_executor) | map(increment);
    };

    for (int i = 1; i < 10; ++i) {
        if (i % 2 == 0) {
            out.receive() | increment_on_executor | detach();
            in.send(meta::unit{}) | detach();
        } else {
            in.send(meta::unit{}) | detach();
            out.receive() | increment_on_executor | detach();
        }
        EXPECT_EQ(counter, i - 1);
        EXPECT_EQ(an_executor.execute_batch(), 1);
        EXPECT_EQ(counter, i);
        EXPECT_EQ(an_executor.execute_batch(), 0);
        EXPECT_EQ(counter, i);
    }

    {
        in.send(meta::unit{}) | detach();
        out.receive() | increment_on_executor | detach();
        an_executor.stop(); // should cancel current receive
        EXPECT_EQ(an_executor.execute_batch(), 0);
        EXPECT_EQ(counter, 9);

        in.send(meta::unit{}) | detach();
        EXPECT_EQ(an_executor.execute_batch(), 0);
        EXPECT_EQ(counter, 9);
    }
}

TEST(algo, cancellableSimple) {
    {
        meta::maybe<meta::result<int, meta::undefined>> maybe_result =
            value_as_signal(42) | cancellable() | get<nowait_event>();
        ASSERT_TRUE(maybe_result.has_value());
        ASSERT_EQ(**maybe_result, 42);
    }

    {
        bool done = false;
        auto connection = value_as_signal(meta::unit{}) //
                          | and_then([](meta::unit) { return meta::ok(meta::unit{}); }) //
                          | cancellable() //
                          | map([&done](meta::unit) {
                                done = true;
                                PANIC("should not be executed");
                                return meta::unit{};
                            })
                          | subscribe();
        auto& cancel_handle = connection.get_cancel_handle();
        ASSERT_TRUE(cancel_handle.intrusive_next);
        ASSERT_TRUE(cancel_handle.intrusive_next->intrusive_next);
        ASSERT_TRUE(cancel_handle.intrusive_next->intrusive_next->intrusive_next);
        EXPECT_FALSE(cancel_handle.intrusive_next->intrusive_next->intrusive_next->intrusive_next);
        EXPECT_TRUE(cancel_handle.try_cancel());

        std::move(connection).emit();
        EXPECT_FALSE(done);
    }

    {
        bool done = false;
        auto connection = value_as_signal(meta::unit{}) | cancellable() | map([&done](meta::unit) {
                              done = true;
                              return meta::unit{};
                          })
                          | subscribe();
        auto& cancel_handle = connection.get_cancel_handle();
        std::move(connection).emit();

        EXPECT_FALSE(cancel_handle.try_cancel());
        EXPECT_TRUE(done);
    }
}

TEST(algo, cancellableSchedule) {
    manual_executor executor;

    struct {
        std::size_t schedule = 0;
        std::size_t map = 0;
    } counters;

    auto connection = //
        schedule(
            executor,
            [&counters] {
                ++counters.schedule;
                return meta::ok(meta::unit{});
            }
        )
        | cancellable() //
        | map([&counters](meta::unit) {
              ++counters.map;
              return meta::unit{};
          })
        | subscribe();

    auto& cancel_handle = connection.get_cancel_handle();
    std::move(connection).emit();

    EXPECT_TRUE(cancel_handle.try_cancel());
    EXPECT_EQ(counters.schedule, 0);
    EXPECT_EQ(counters.map, 0);

    EXPECT_EQ(executor.execute_batch(), 1);
    EXPECT_EQ(counters.schedule, 1);
    EXPECT_EQ(counters.map, 0);
    EXPECT_EQ(executor.execute_batch(), 0);
}

TEST(algo, cancellablePipe) {
    auto [in, out] = make_pipe<meta::unit, meta::undefined>();
    std::size_t counter = 0;

    auto connection = //
        out.receive() //
        | map([&counter](meta::unit) {
              ++counter;
              return meta::unit{};
          })
        | subscribe();
    auto& cancel_handle = connection.get_cancel_handle();
    EXPECT_TRUE(cancel_handle.intrusive_next);
    EXPECT_TRUE(cancel_handle.intrusive_next->intrusive_next);

    std::move(connection).emit();
    EXPECT_TRUE(cancel_handle.try_cancel());

    in.send(meta::unit{}) | detach();
    EXPECT_EQ(counter, 0);

    // would ASSERT
    // in.send(meta::unit{}) | detach();
}

TEST(algo, cancellablePipeAll) {
    std::size_t counter1 = 0;
    std::size_t counter2 = 0;
    std::size_t counter_all = 0;

    auto [in1, out1] = make_pipe<meta::unit, meta::unit>();
    auto [in2, out2] = make_pipe<meta::unit, meta::unit>();

    auto signal1 = out1.receive() | map_error([&counter1](meta::unit) {
                       ++counter1;
                       return meta::unit{};
                   });
    auto signal2 = out2.receive() | map_error([&counter2](meta::unit) {
                       ++counter2;
                       return meta::unit{};
                   });

    all(std::move(signal1), std::move(signal2)) | map_error([&counter_all](meta::unit) {
        ++counter_all;
        return meta::unit{};
    }) | detach();

    in1.send(meta::err(meta::unit{})) | detach();
    EXPECT_EQ(counter1, 1);
    EXPECT_EQ(counter2, 0);
    EXPECT_EQ(counter_all, 1);

    in2.send(meta::err(meta::unit{})) | detach();
    EXPECT_EQ(counter1, 1);
    EXPECT_EQ(counter2, 0);
    EXPECT_EQ(counter_all, 1);

    // will ASSERT
    // in2.send(meta::err(meta::unit{})) | detach();

    out2.receive() | map_error([&counter2](meta::unit) {
        ++counter2;
        return meta::unit{};
    }) | detach();
    EXPECT_EQ(counter1, 1);
    EXPECT_EQ(counter2, 1);
    EXPECT_EQ(counter_all, 1);
}

TEST(algo, cancellablePipeAny) {
    std::size_t counter1 = 0;
    std::size_t counter2 = 0;
    std::size_t counter_any = 0;

    auto [in1, out1] = make_pipe<meta::unit, meta::unit>();
    auto [in2, out2] = make_pipe<meta::unit, meta::unit>();

    auto signal1 = out1.receive() | map_error([&counter1](meta::unit) {
                       ++counter1;
                       return meta::unit{};
                   });
    auto signal2 = out2.receive() | map_error([&counter2](meta::unit) {
                       ++counter2;
                       return meta::unit{};
                   });

    any(std::move(signal1), std::move(signal2)) | map_error([&counter_any](meta::unit) {
        ++counter_any;
        return meta::unit{};
    }) | detach();

    in1.send(meta::err(meta::unit{})) | detach();
    EXPECT_EQ(counter1, 1);
    EXPECT_EQ(counter2, 0);
    EXPECT_EQ(counter_any, 0);

    in2.send(meta::err(meta::unit{})) | detach();
    EXPECT_EQ(counter1, 1);
    EXPECT_EQ(counter2, 1);
    EXPECT_EQ(counter_any, 1);

    // will not ASSERT
    in2.send(meta::err(meta::unit{})) | detach();

    out2.receive() | map_error([&counter2](meta::unit) {
        ++counter2;
        return meta::unit{};
    }) | detach();
    EXPECT_EQ(counter1, 1);
    EXPECT_EQ(counter2, 2);
    EXPECT_EQ(counter_any, 1);
}

TEST(algo, cancellablePipeCombinatorsCancelled) {
    auto [in, out] = make_pipe<meta::unit, meta::undefined>();
    std::size_t counter = 0;

    auto connection = //
        out.receive() //
        | map([&counter](meta::unit) {
              ++counter;
              return meta::unit{};
          })
        | subscribe();

    auto& cancel_handle = connection.get_cancel_handle();
    EXPECT_TRUE(cancel_handle.intrusive_next);
    EXPECT_TRUE(cancel_handle.intrusive_next->intrusive_next);

    std::move(connection).emit();
    EXPECT_TRUE(cancel_handle.try_cancel());

    in.send(meta::unit{}) | detach();
    EXPECT_EQ(counter, 0);

    // would ASSERT
    // in.send(meta::unit{}) | detach();
}

TEST(algo, channelSimple) {
    auto channel = make_channel<int>();

    std::size_t counter = 0;
    const auto increment = [&](int value) {
        ++counter;
        return value;
    };

    channel->send(42) | detach();

    {
        const auto result = channel->receive() | map(increment) | get<nowait_event>();
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result.value(), 42);
        EXPECT_EQ(counter, 1);
    }

    channel->receive() | map(increment) | detach();
    EXPECT_EQ(counter, 1);
    channel->send(69) | detach();
    EXPECT_EQ(counter, 2);

    for (int i = 3; i < 10; ++i) {
        channel->send(int{ i }) | detach();
    }
    EXPECT_EQ(counter, 2);

    for (int i = 3; i < 10; ++i) {
        const auto result = channel->receive() | map(increment) | get<nowait_event>();
        ASSERT_TRUE(result.has_value());
        EXPECT_EQ(result.value(), i);
        EXPECT_EQ(counter, i);
    }
}

TEST(algo, selectAsAny) {
    manual_executor executor;
    std::size_t counter1 = 0;
    std::size_t counter2 = 0;
    std::size_t counter3 = 0;
    auto s1 = as_signal(meta::result<int, meta::unit>{ 69 });
    auto s2 = as_signal(meta::result<meta::unit, meta::unit>{}) | continue_on(executor)
              | map([](meta::unit) { return std::string{ "42" }; });
    auto s3 = as_signal(meta::result<int, meta::unit>{ 72 });
    auto maybe_result = select()
                            .case_(
                                std::move(s1),
                                [&counter1](int value) {
                                    ++counter1;
                                    return std::to_string(value);
                                }
                            )
                            .case_(
                                std::move(s2),
                                [&counter2](auto value) {
                                    ++counter2;
                                    return value;
                                }
                            )
                            .case_(
                                std::move(s3),
                                [&counter3](int value) {
                                    ++counter3;
                                    return std::to_string(value);
                                }
                            )
                        | get<nowait_event>();
    EXPECT_EQ(maybe_result->value(), "69");
    EXPECT_EQ(counter1, 1);
    EXPECT_EQ(counter2, 0);
    EXPECT_EQ(counter3, 0);

    executor.execute_batch();
    EXPECT_EQ(counter1, 1);
    EXPECT_EQ(counter2, 0);
    EXPECT_EQ(counter3, 0);
}

TEST(algo, selectChannel) {
    std::size_t counter1 = 0;
    std::size_t counter2 = 0;
    std::size_t counter3 = 0;
    auto channel1 = make_channel<int>();
    auto channel2 = make_channel<std::string>();
    auto channel3 = make_channel<double>();
    select() //
            .case_(
                channel1->send(42),
                [&counter1](meta::unit) {
                    ++counter1;
                    return meta::unit{};
                }
            )
            .case_(
                channel2->receive(),
                [&counter2](std::string) {
                    ++counter2;
                    return meta::unit{};
                }
            )
            .case_(
                channel3->send(4.0),
                [&counter3](meta::unit) {
                    ++counter3;
                    return meta::unit{};
                }
            )
        | detach();
    EXPECT_EQ(counter1, 0);
    EXPECT_EQ(counter2, 0);
    EXPECT_EQ(counter3, 0);

    const auto c2_send_result = channel2->send("hehe") | get<nowait_event>();
    EXPECT_TRUE(c2_send_result.has_value());
    EXPECT_TRUE(c2_send_result->has_value());
    EXPECT_EQ(counter1, 0);
    EXPECT_EQ(counter2, 1);
    EXPECT_EQ(counter3, 0);

    channel2->send("hehe") | map([&counter2](meta::unit) {
        ++counter2;
        return meta::unit{};
    }) | detach();
    EXPECT_EQ(counter1, 0);
    EXPECT_EQ(counter2, 1);
    EXPECT_EQ(counter3, 0);

    channel1->receive() | map([&counter1](int) {
        ++counter1;
        return meta::unit{};
    }) | detach();
    EXPECT_EQ(counter1, 0);
    EXPECT_EQ(counter2, 1);
    EXPECT_EQ(counter3, 0);

    channel3->receive() | map([&counter3](double) {
        ++counter3;
        return meta::unit{};
    }) | detach();
    EXPECT_EQ(counter1, 0);
    EXPECT_EQ(counter2, 1);
    EXPECT_EQ(counter3, 0);
}

TEST(algo, selectChannelDefault) {
    std::size_t counter1 = 0;
    std::size_t counter2 = 0;
    std::size_t counter3 = 0;
    std::size_t counter_default = 0;
    auto channel1 = make_channel<int>();
    auto channel2 = make_channel<std::string>();
    auto channel3 = make_channel<double>();

    const auto result_default = //
        select() //
            .case_(
                channel1->send(42),
                [&counter1](meta::unit) {
                    ++counter1;
                    return meta::unit{};
                }
            )
            .case_(
                channel2->receive(),
                [&counter2](std::string) {
                    ++counter2;
                    return meta::unit{};
                }
            )
            .case_(
                channel3->send(4.0),
                [&counter3](meta::unit) {
                    ++counter3;
                    return meta::unit{};
                }
            )
            .default_([&counter_default](meta::unit) {
                ++counter_default;
                return meta::unit{};
            })
        | get<nowait_event>();
    ASSERT_TRUE(result_default.has_value());
    EXPECT_TRUE(result_default->has_value());
    EXPECT_EQ(counter1, 0);
    EXPECT_EQ(counter2, 0);
    EXPECT_EQ(counter3, 0);
    EXPECT_EQ(counter_default, 1);

    channel2->send("hehe") | detach();
    const auto result_channel2 = //
        select() //
            .case_(
                channel1->send(42),
                [&counter1](meta::unit) {
                    ++counter1;
                    return meta::unit{};
                }
            )
            .case_(
                channel2->receive(),
                [&counter2](std::string) {
                    ++counter2;
                    return meta::unit{};
                }
            )
            .case_(
                channel3->send(4.0),
                [&counter3](meta::unit) {
                    ++counter3;
                    return meta::unit{};
                }
            )
            .default_([&counter_default](meta::unit) {
                ++counter_default;
                return meta::unit{};
            })
        | get<nowait_event>();
    ASSERT_TRUE(result_channel2.has_value());
    EXPECT_TRUE(result_channel2->has_value());
    EXPECT_EQ(counter1, 0);
    EXPECT_EQ(counter2, 1);
    EXPECT_EQ(counter3, 0);
    EXPECT_EQ(counter_default, 1);
}

TEST(algo, channelCloseSend) {
    auto channel = make_channel<int>();
    channel->send(1) | detach();
    channel->send(2) | detach();

    const auto result_close = channel->close() | get<nowait_event>();
    EXPECT_TRUE(result_close->has_value());

    const auto result_close_retry = channel->close() | get<nowait_event>();
    EXPECT_FALSE(result_close_retry->has_value());

    const auto result_receive1 = channel->receive() | get<nowait_event>();
    ASSERT_TRUE(result_receive1->has_value());
    EXPECT_EQ(result_receive1->value(), 1);

    const auto result_send_after_close = channel->send(3) | get<nowait_event>();
    EXPECT_FALSE(result_send_after_close->has_value());

    const auto result_receive2 = channel->receive() | get<nowait_event>();
    ASSERT_TRUE(result_receive2->has_value());
    EXPECT_EQ(result_receive2->value(), 2);

    const auto result_receive_empty = channel->receive() | get<nowait_event>();
    EXPECT_FALSE(result_receive_empty->has_value());
}

TEST(algo, channelCloseReceive) {
    std::size_t counter1 = 0;
    std::size_t counter_err1 = 0;
    std::size_t counter2 = 0;
    std::size_t counter_err2 = 0;

    auto channel = make_channel<int>();
    channel->receive() | map([&counter1](int) {
        ++counter1;
        return meta::unit{};
    }) | map_error([&counter_err1](meta::unit) {
        ++counter_err1;
        return meta::unit{};
    }) | detach();
    channel->receive() | map([&counter2](int) {
        ++counter2;
        return meta::unit{};
    }) | map_error([&counter_err2](meta::unit) {
        ++counter_err2;
        return meta::unit{};
    }) | detach();

    const auto result_before_close = channel->send(1) | get<nowait_event>();
    EXPECT_TRUE(result_before_close->has_value());
    EXPECT_EQ(counter1, 1);
    EXPECT_EQ(counter_err1, 0);
    EXPECT_EQ(counter2, 0);
    EXPECT_EQ(counter_err2, 0);

    const auto result_close = channel->close() | get<nowait_event>();
    EXPECT_TRUE(result_close->has_value());
    EXPECT_EQ(counter1, 1);
    EXPECT_EQ(counter_err1, 0);
    EXPECT_EQ(counter2, 0);
    EXPECT_EQ(counter_err2, 1);

    const auto result_after_close = channel->send(2) | get<nowait_event>();
    EXPECT_FALSE(result_after_close->has_value());
    EXPECT_EQ(counter1, 1);
    EXPECT_EQ(counter_err1, 0);
    EXPECT_EQ(counter2, 0);
    EXPECT_EQ(counter_err2, 1);
}

TEST(algo, selectSingleChannel) {
    std::size_t send_counter = 0;
    std::size_t receive_counter = 0;
    auto channel = make_channel<int>();

    {
        select() //
                .case_(
                    channel->receive(),
                    [&receive_counter](int) {
                        ++receive_counter;
                        return meta::unit{};
                    }
                )
                .case_(
                    channel->send(42),
                    [&send_counter](meta::unit) {
                        ++send_counter;
                        return meta::unit{};
                    }
                )
            | detach();
        EXPECT_EQ(send_counter, 0);
        EXPECT_EQ(receive_counter, 0);
    }
    {
        // or leftover_receive, doesn't matter, one of select branches would fire
        const auto leftover_send = channel->receive() | get<nowait_event>();
        ASSERT_TRUE(leftover_send.has_value());
        EXPECT_EQ(send_counter, 1);
        EXPECT_EQ(receive_counter, 0);
    }
    {
        select() //
                .case_(
                    channel->send(42),
                    [&send_counter](meta::unit) {
                        ++send_counter;
                        return meta::unit{};
                    }
                )
                .case_(
                    channel->receive(),
                    [&receive_counter](int) {
                        ++receive_counter;
                        return meta::unit{};
                    }
                )
            | detach();
        EXPECT_EQ(send_counter, 1);
        EXPECT_EQ(receive_counter, 0);
    }
    {
        // or leftover_send, doesn't matter, one of select branches would fire
        const auto leftover_receive = channel->send(42) | get<nowait_event>();
        ASSERT_TRUE(leftover_receive.has_value());
        EXPECT_EQ(send_counter, 1);
        EXPECT_EQ(receive_counter, 1);
    }
}

TEST(algo, selectSingleChannelWithDefault) {
    std::size_t send_counter = 0;
    std::size_t receive_counter = 0;
    std::size_t default_counter = 0;
    auto channel = make_channel<int>();

    {
        const auto result = select()
                                .case_(
                                    channel->receive(),
                                    [&receive_counter](int) {
                                        ++receive_counter;
                                        return meta::unit{};
                                    }
                                )
                                .case_(
                                    channel->send(42),
                                    [&send_counter](meta::unit) {
                                        ++send_counter;
                                        return meta::unit{};
                                    }
                                )
                                .default_([&default_counter](meta::unit) {
                                    ++default_counter;
                                    return meta::unit{};
                                })
                            | get<nowait_event>();

        ASSERT_TRUE(result.has_value());
        EXPECT_TRUE(result->has_value());
        EXPECT_EQ(send_counter, 0);
        EXPECT_EQ(receive_counter, 0);
        EXPECT_EQ(default_counter, 1);
    }
    {
        const auto result = select()
                                .case_(
                                    channel->send(42),
                                    [&send_counter](meta::unit) {
                                        ++send_counter;
                                        return meta::unit{};
                                    }
                                )
                                .case_(
                                    channel->receive(),
                                    [&receive_counter](int) {
                                        ++receive_counter;
                                        return meta::unit{};
                                    }
                                )
                                .default_([&default_counter](meta::unit) {
                                    ++default_counter;
                                    return meta::unit{};
                                })
                            | get<nowait_event>();

        ASSERT_TRUE(result.has_value());
        EXPECT_TRUE(result->has_value());
        EXPECT_EQ(send_counter, 0);
        EXPECT_EQ(receive_counter, 0);
        EXPECT_EQ(default_counter, 2);
    }

    channel->receive() | map([&receive_counter](int) {
        ++receive_counter;
        return meta::unit{};
    }) | detach();
    EXPECT_EQ(send_counter, 0);
    EXPECT_EQ(receive_counter, 0);
    EXPECT_EQ(default_counter, 2);
    channel->send(42) | map([&send_counter](meta::unit) {
        ++send_counter;
        return meta::unit{};
    }) | detach();
    EXPECT_EQ(send_counter, 1);
    EXPECT_EQ(receive_counter, 1);
    EXPECT_EQ(default_counter, 2);
}

} // namespace sl::exec
