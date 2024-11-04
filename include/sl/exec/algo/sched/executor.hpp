//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/model/concept.hpp"
#include "sl/exec/model/executor.hpp"

namespace sl::exec {
namespace detail {

struct [[nodiscard]] executor_connection : task_node {
    executor_connection(slot<sl::meta::unit, sl::meta::undefined>& slot, executor& executor)
        : slot_{ slot }, executor_{ executor } {}

    void emit() { executor_.schedule(this); }

    void execute() noexcept override { slot_.set_value(sl::meta::unit{}); }
    void cancel() noexcept override { slot_.cancel(); }

private:
    slot<sl::meta::unit, sl::meta::undefined>& slot_;
    executor& executor_;
};

struct [[nodiscard]] executor_signal {
    using value_type = sl::meta::unit;
    using error_type = sl::meta::undefined;

    executor& executor_;

    Connection auto subscribe(slot<value_type, error_type>& slot) && {
        return executor_connection{
            /* .slot = */ slot,
            /* .executor = */ get_executor(),
        };
    }

    executor& get_executor() { return executor_; }
};

struct [[nodiscard]] executor_scheduler {
    executor& executor;

    constexpr Signal auto schedule() { return executor_signal{ executor }; }
};

} // namespace detail

constexpr Scheduler auto as_scheduler(executor& executor) { return detail::executor_scheduler{ executor }; }

} // namespace sl::exec
