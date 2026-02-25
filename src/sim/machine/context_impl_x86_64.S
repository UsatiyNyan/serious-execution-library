# https://github.com/UsatiyNyan/serious-execution-library/issues/2

#ifdef SL_OS_IS_macos
  #define SL_SIM_ASM_SYMBOL(name) _##name
#else
  #define SL_SIM_ASM_SYMBOL(name) name
#endif

.global SL_SIM_ASM_SYMBOL(sl_sim_context_setup)
.global SL_SIM_ASM_SYMBOL(sl_sim_context_switch)

# void* sl_sim_context_setup(void* stack, void* injection, void* trampoline);
# %rdi - stack
# %rsi - injection
# %rdx - trampoline
SL_SIM_ASM_SYMBOL(sl_sim_context_setup):
    # 1. save current Stack
    movq %rsp, %r11
    # 1. switch to new Stack
    movq %rdi, %rsp

    # 2.1. make space for "red zone"
    subq $64, %rsp

    # 2.2. align Stack Pointer to 16B, as per ABI
    andq $-16, %rsp
    # 4. misalign by 8B for the push of Return Address
    addq $8, %rsp

    # 3. store `trampoline` in `%rsp + 8` - above the Return Address
    movq %rdx, 8(%rsp)

    # 2.2. and 4. push `injection` Return Address, now Stack Pointer is aligned to 16B
    pushq %rsi

    # 5. setup Registers
    # r15, r14, r13, r12, rbx, rbp
    pushq $0
    pushq $0
    pushq $0
    pushq $0
    pushq $0
    pushq $0

    # 6. save new Stack Pointer as return value
    movq %rsp, %rax

    # 7. restore callstack
    movq %r11, %rsp

    retq

# void sl_sim_context_switch(void** from, void** to);
# %rdi - from
# %rsi - to
SL_SIM_ASM_SYMBOL(sl_sim_context_switch):
    # save Callee-Saved Registers
    pushq %r15
    pushq %r14
    pushq %r13
    pushq %r12
    pushq %rbx
    pushq %rbp

    # save `from` Stack
    movq %rsp, (%rdi)
    # activate `to` Stack
    movq (%rsi), %rsp

    # restore Callee-Saved Registers
    popq %rbp
    popq %rbx
    popq %r12
    popq %r13
    popq %r14
    popq %r15

    # return into `injection` or `sl_sim_context_switch`
    retq

#ifndef SL_OS_IS_macos
.section .note.GNU-stack, "", @progbits
#endif
