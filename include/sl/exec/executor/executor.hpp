//
// Created by usatiynyan.
// TODO: think about stop() for an interface
//

#pragma once

#include "sl/exec/conn/concept.hpp"
#include "sl/exec/executor/task.hpp"

namespace sl::exec {

struct executor {
    struct scheduler;

    virtual ~executor() noexcept = default;
    virtual void schedule(task_node* task_node) noexcept = 0;
};

struct executor::scheduler {
    template <typename Slot>
    struct [[nodiscard]] connection : task_node {
        Slot slot;
        executor& executor;

        // vvv public API
        void emit() { executor.schedule(this); }

        // vvv private API
        void execute() noexcept override { slot.set_result(std::monostate{}); }
        void cancel() noexcept override { slot.cancel(); }
    };

    struct [[nodiscard]] signal {
        using result_type = std::monostate;

        executor& executor;

        template <Slot<result_type> SlotT>
        Connection auto connect(SlotT slot) {
            return connection{ .slot = std::move(slot), .executor = executor };
        }
    };

    executor& executor;

    auto schedule() { return signal{ .executor = executor }; }
};

inline Scheduler auto as_scheduler(executor& a_executor) { return executor::scheduler{ .executor = a_executor }; }

} // namespace sl::exec
