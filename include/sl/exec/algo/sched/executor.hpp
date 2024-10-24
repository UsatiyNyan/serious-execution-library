//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/model/concept.hpp"
#include "sl/exec/model/executor.hpp"

namespace sl::exec {
namespace detail {

template <typename Slot>
struct [[nodiscard]] executor_connection : task_node {
    Slot slot;
    executor& executor;

    void emit() { executor.schedule(this); }

    void execute() noexcept override { slot.set_value(sl::meta::unit{}); }
    void cancel() noexcept override { slot.cancel(); }
};

struct [[nodiscard]] executor_signal {
    using value_type = sl::meta::unit;
    using error_type = sl::meta::undefined;

    executor& executor_;

    template <Slot<value_type, error_type> SlotT>
    Connection auto subscribe(SlotT&& slot) & noexcept {
        return executor_connection{
            .slot = std::move(slot),
            .executor = get_executor(),
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
