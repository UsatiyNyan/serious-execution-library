//
// Created by usatiynyan.
//

#pragma once

#if SL_EXEC_SIM

#include "sl/exec/model/task.hpp"
#include "sl/exec/sim/stack.hpp"

#include <sl/meta/assert.hpp>

namespace sl::exec::sim {

struct machine_context {
    explicit machine_context() = default;
    // only will call `trampoline.execute()`
    static machine_context setup(stack& a_stack, task& trampoline);
    void switch_to(machine_context& target);

    [[nodiscard]] void* unsafe_rsp() const { return rsp_; }

public:
    machine_context(const machine_context&) = delete;
    machine_context& operator=(const machine_context&) = delete;
    machine_context& operator=(machine_context&& other) noexcept = delete;

    machine_context(machine_context&& other) noexcept : rsp_{ std::exchange(other.rsp_, nullptr) } {}

private:
    explicit machine_context(void* rsp) : rsp_{ rsp } {}

private:
    void* rsp_ = nullptr;
};

} // namespace sl::exec::sim

#endif
