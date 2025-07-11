//
// Created by usatiynyan.
//

#include "sl/exec/thread/detail/multiword_dcss.hpp"

namespace sl::exec::detail {

SL_EXEC_DESCRIPTOR_POOL_TEMPLATE_INSTANTIATE(dcss_descriptor)

// DCSS(a1, e1, a2, e2, n2) :
//   des := CreateNew(DCSSdes, a1, e1, a2, e2, n2)
//   fdes := flag(des)
//   loop
//     r := CAS(a2, e2, f des)
//     if r is flagged then DCSSHelp(r)
//     else exit loop
//   if r = e2 then DCSSHelp(f des)
//   return r
std::uintptr_t dcss(
    detail::atomic<std::uintptr_t>* a1,
    std::uintptr_t e1,
    detail::atomic<std::uintptr_t>* a2,
    std::uintptr_t e2,
    std::uintptr_t n2
) {
    DEBUG_ASSERT(
        !dcss_check_flag(e1) && !dcss_check_flag(e2) && !dcss_check_flag(n2),
        "algo wouldn't work with highest bit set in initial values"
    );

    const mw::pointer_type des = mw::create_new<dcss_descriptor>(
        mw::state_type{},
        dcss_descriptor::immutables_type{
            .a1 = a1,
            .e1 = e1,
            .a2 = a2,
            .e2 = e2,
            .n2 = n2,
        }
    );

    const mw::pointer_type fdes = dcss_set_flag(des, true);

    std::uintptr_t r{};
    while (true) {
        r = e2;
        if (a2->compare_exchange_weak(r, fdes, std::memory_order::relaxed) || !dcss_check_flag(r)) {
            break;
        }
        dcss_help(r);
    }

    if (r == e2) {
        dcss_help(fdes);
    }

    return r;
}

// DCSSRead(addr) :
//   loop
//     r := ∗addr
//     if r is flagged then DCSSHelp(r)
//     else exit loop
//   return r
std::uintptr_t dcss_read(detail::atomic<std::uintptr_t>* a) {
    while (true) {
        const std::uintptr_t r = a->load(std::memory_order::relaxed);
        if (!dcss_check_flag(r)) {
            return r;
        }
        dcss_help(r);
    }
}

// DCSSHelp(fdes) :
//   des := Unflag(fdes)
//   values := ReadImmutables(des)
//   if values = ⊥ then return
//   <a1, e1, a2, e2, n2> := values
//   if ∗a1 = e1 then
//     CAS(a2, fdes, n2)
//   else
//     CAS(a2, fdes, e2)
void dcss_help(mw::pointer_type fdes) {
    DEBUG_ASSERT(dcss_check_flag(fdes));

    const auto values = mw::read_immutables<dcss_descriptor>(fdes);
    if (!values.has_value()) { // is bottom
        return;
    }

    const auto [a1, e1, a2, e2, n2] = values.value();
    if (a1->load(std::memory_order::relaxed) == e1) {
        a2->compare_exchange_weak(fdes, n2);
    } else {
        a2->compare_exchange_weak(fdes, e2);
    }
}

// choose highest flag bit
static constexpr mw::pointer_type dcss_flag_bit = 1 << (dcss_descriptor_pointer_traits::flag_width - 1);

bool dcss_check_flag(mw::pointer_type des) {
    const auto [flag, pid, seq] = dcss_descriptor_pointer_traits::extract(des);
    return (flag & dcss_flag_bit) == dcss_flag_bit;
}

mw::pointer_type dcss_set_flag(mw::pointer_type des, bool flag_value) {
    DEBUG_ASSERT(dcss_check_flag(des) == !flag_value);
    const auto [flag, pid, seq] = dcss_descriptor_pointer_traits::extract(des);
    const mw::pointer_type clear_flag = ~dcss_flag_bit & dcss_descriptor_pointer_traits::flag_mask;
    const mw::pointer_type new_flag_bit = flag_value ? dcss_flag_bit : 0;
    const mw::pointer_type new_flag = (flag & clear_flag) | new_flag_bit;
    return dcss_descriptor_pointer_traits::combine(new_flag, pid, seq);
}

} // namespace sl::exec::detail
