# https://github.com/UsatiyNyan/serious-execution-library/issues/2

#ifdef SL_OS_IS_macos
  #define SL_SIM_ASM_SYMBOL(name) _##name
#else
  #define SL_SIM_ASM_SYMBOL(name) name
#endif

.p2align 4 # align to 16 bytes
.global SL_SIM_ASM_SYMBOL(sl_sim_context_setup)
.global SL_SIM_ASM_SYMBOL(sl_sim_context_switch)

# void* sl_sim_context_setup(void* stack, void* injection, void* trampoline);
# x0 - stack
# x1 - injection
# x2 - trampoine
SL_SIM_ASM_SYMBOL(sl_sim_context_setup):
    # 1. save current Stack
    mov     x3, sp
    # 1. switch to new Stack
    mov     sp, x0

    # 2.1. make space for "red zone"
    sub     sp, sp, #64

    # 2.2. align Stack Pointer to 16B, as per ABI
    mov     x4, sp
    and     x4, x4, #-16
    mov     sp, x4

    # 3. and 4. sp += 16 for the next pushes of `trampoline` and `injection`
    add     sp, sp, #16

    # 3. sp -= 16 and store `trampoline` - above the Return Address
    str     x2, [sp, #-16]!

    # 2.2. and 4. sp -= 16 and store `injection` Return Address
    str     x1, [sp, #-16]!

    # 5. setup Registers
    # x30-x20, d8-d15
    mov     x4, #0
    stp     x4, x4, [sp, #-16]!
    stp     x4, x4, [sp, #-16]!
    stp     x4, x4, [sp, #-16]!
    stp     x4, x4, [sp, #-16]!
    stp     x4, x4, [sp, #-16]!
    stp     x4, x4, [sp, #-16]!
    stp     x4, x4, [sp, #-16]!
    stp     x4, x4, [sp, #-16]!
    stp     x4, x4, [sp, #-16]!

    # 6. save new Stack Pointer as return value
    mov     x0, sp

    # 7. restore current Stack
    mov     sp, x3

    ret

# void sl_sim_context_switch(void** from, void** to);
# x0 - from
# x1 - to
SL_SIM_ASM_SYMBOL(sl_sim_context_switch):
    # save Callee-Saved Registers
    stp     x30, x29, [sp, #-16]!
    stp     x20, x19, [sp, #-16]!
    stp     x22, x21, [sp, #-16]!
    stp     x24, x23, [sp, #-16]!
    stp     x26, x25, [sp, #-16]!
    stp     x28, x27, [sp, #-16]!

    stp     d8,  d9,  [sp, #-16]!
    stp     d10, d11, [sp, #-16]!
    stp     d12, d13, [sp, #-16]!
    stp     d14, d15, [sp, #-16]!

    # save `from` Stack
    mov     x2, sp # tmp = sp
    str     x2, [x0] # *from = tmp

    # activate `to` Stack
    ldr     x2, [x1] # tmp = *to
    mov     sp, x2 # sp = tmp

    # restore Callee-Saved Registers
    ldp     d14, d15, [sp], #16
    ldp     d12, d13, [sp], #16
    ldp     d10, d11, [sp], #16
    ldp     d8,  d9,  [sp], #16
  
    ldp     x28, x27, [sp], #16
    ldp     x26, x25, [sp], #16
    ldp     x24, x23, [sp], #16
    ldp     x22, x21, [sp], #16
    ldp     x20, x19, [sp], #16
    ldp     x30, x29, [sp], #16

    # return into `injection` or `sl_sim_context_switch`
    ret
