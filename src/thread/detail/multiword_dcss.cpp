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
    std::uintptr_t a1,
    dcss_a1_load_type a1_load,
    std::uintptr_t e1,
    detail::atomic<std::uintptr_t>* a2,
    std::uintptr_t e2,
    std::uintptr_t n2
) {
    DEBUG_ASSERT(
        !mw::has_flag<dcss_descriptor>(e1) && !mw::has_flag<dcss_descriptor>(e2) && !mw::has_flag<dcss_descriptor>(n2),
        "algo wouldn't work with highest bit set in initial values"
    );

    const mw::pointer_type des = mw::create_new<dcss_descriptor>(
        mw::state_type{}, dcss_descriptor::immutables_type{ a1, a1_load, e1, a2, e2, n2 }
    );

    const mw::pointer_type fdes = mw::set_flag<dcss_descriptor>(des);

    std::uintptr_t r{};
    while (true) {
        r = e2;
        if (a2->compare_exchange_weak(r, fdes, std::memory_order::relaxed) || !mw::has_flag<dcss_descriptor>(r)) {
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
        if (!mw::has_flag<dcss_descriptor>(r)) {
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
    const mw::pointer_type des = mw::unset_flag<dcss_descriptor>(fdes);
    const auto values = mw::read_immutables<dcss_descriptor>(des);
    if (!values.has_value()) { // is bottom
        return;
    }

    const auto [a1, a1_load, e1, a2, e2, n2] = values.value();
    if (a1_load(a1) == e1) {
        a2->compare_exchange_weak(fdes, n2);
    } else {
        a2->compare_exchange_weak(fdes, e2);
    }
}

} // namespace sl::exec::detail
