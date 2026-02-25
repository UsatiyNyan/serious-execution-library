//
// Created by usatiynyan.
//

#pragma once

#if SL_EXEC_SIM

extern "C" {

void* sl_sim_context_setup(void* stack, void* injection, void* trampoline);

void sl_sim_context_switch(void** from, void** to);
}

#endif
