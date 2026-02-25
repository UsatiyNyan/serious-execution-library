//
// Created by usatiynyan.
// https://github.com/UsatiyNyan/serious-execution-library/issues/2
//

#include "sl/exec/sim/machine/context.hpp"
#include "sl/exec/sim/machine/context_impl.hpp"

namespace sl::exec::sim {

#ifdef SL_CPU_IS_x86_64
static void machine_injection(void*, void*, void*, void*, void*, void*, void* t) { static_cast<task*>(t)->execute(); };
#elifdef SL_CPU_IS_arm
static void machine_injection(void*, void*, void*, void*, void*, void*, void*, void*, void* t) {
    static_cast<task*>(t)->execute();
};
#else
#error "not implemented"
#endif

machine_context machine_context::setup(stack& a_stack, task& trampoline) {
    void* stack = a_stack.user_page().data() + a_stack.user_page().size();
    void* rsp = sl_sim_context_setup(stack, std::bit_cast<void*>(&machine_injection), &trampoline);
    return machine_context{ rsp };
}

void machine_context::switch_to(machine_context& target) { sl_sim_context_switch(&rsp_, &target.rsp_); }

} // namespace sl::exec::sim
