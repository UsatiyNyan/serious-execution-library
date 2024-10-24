//
// Created by usatiynyan.
//

#include "sl/exec/algo.hpp"
#include "sl/exec/model.hpp"

#include <gtest/gtest.h>

namespace sl::exec {

TEST(conn, valueSignal) {
    const meta::result<int, meta::unit> result = //
        as_signal(meta::result<int, meta::unit>(42)) //
        | get<nowait_event>();
    ASSERT_EQ(result, 42);
}

TEST(algo, andThen) {
    const meta::result<std::string, meta::unit> result =
        as_signal(meta::result<int, meta::unit>(42)) //
        | and_then([](int i) { return meta::result<int, meta::unit>{ i + 1 }; }) //
        | and_then([](int i) { return meta::result<std::string, meta::unit>{ std::to_string(i) }; }) //
        | get<nowait_event>();
    ASSERT_EQ(result, "43") << result.value();
}

TEST(algo, map) {
    const meta::result<std::string, meta::unit> result = //
        as_signal(meta::result<int, meta::unit>(42)) //
        | map([](int i) { return i + 1; }) //
        | map([](int i) { return std::to_string(i); }) //
        | get<nowait_event>();
    ASSERT_EQ(result, "43") << result.value();
}

TEST(algo, mapError) {
    const meta::result<meta::unit, std::string> result = //
        as_signal(meta::result<meta::unit, int>(meta::err(42))) //
        | map_error([](int i) { return i + 1; }) //
        | map_error([](int i) { return std::to_string(i); }) //
        | get<nowait_event>();
    ASSERT_EQ(result, meta::err(std::string{ "43" })) << result.error();
}

TEST(algo, orElse) {
    const meta::result<meta::unit, std::string> result = //
        as_signal(meta::result<meta::unit, int>(meta::err(42))) //
        | or_else([](int i) { return meta::result<meta::unit, int>(meta::err(i + 1)); }) //
        | or_else([](int i) { return meta::result<meta::unit, std::string>(meta::err(std::to_string(i))); }) //
        | get<nowait_event>();
    ASSERT_EQ(result, meta::err(std::string{ "43" })) << result.error();
}

} // namespace sl::exec
