//
// Created by usatiynyan.
//

#include "sl/exec/conn.hpp"

#include <gtest/gtest.h>

namespace sl::exec {

TEST(conn, functorSlot) {
    bool ok = false;
    auto slot = as_slot([&](bool ok_result) { ok = ok_result; });
    ASSERT_FALSE(ok);
    std::move(slot).set_result(true);
    ASSERT_TRUE(ok);
}

TEST(conn, valueSignal) {
    std::optional<int> maybe_i{};
    auto signal = as_signal(42);
    auto slot = as_slot([&](int i_result) { maybe_i = i_result; });
    auto connection = connect(std::move(signal), std::move(slot));
    ASSERT_FALSE(maybe_i.has_value());
    std::move(connection).emit();
    ASSERT_EQ(maybe_i, 42);
}

TEST(conn, inlineScheduler) {
    bool ok = false;
    inline_scheduler scheduler;
    auto signal = scheduler.schedule();
    auto slot = as_slot([&](sl::meta::unit) { ok = true; });
    auto connection = connect(std::move(signal), std::move(slot));
    ASSERT_FALSE(ok);
    std::move(connection).emit();
    ASSERT_TRUE(ok);
}

} // namespace sl::exec
