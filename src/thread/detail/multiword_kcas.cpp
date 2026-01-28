//
// Created by usatiynyan.
//

#include "sl/exec/thread/detail/multiword_kcas.hpp"
#include "sl/exec/thread/detail/multiword.hpp"

namespace sl::exec::detail {

template <>
mw::descriptor_pool<kcas_descriptor>::descriptors_type //
    mw::descriptor_pool<kcas_descriptor>::descriptors{};

bool kcas(kcas_descriptor::immutables_type immutables) {
    const mw::pointer_type des = mw::create_new<kcas_descriptor>(kcas_descriptor::state_undecided, immutables);
    const mw::pointer_type fdes = mw::set_flag<kcas_descriptor>(des);
    const auto result = kcas_help(fdes);
    DEBUG_ASSERT(result.has_value());
    return result.value();
}

std::uintptr_t kcas_read(detail::atomic<std::uintptr_t>* a) {
    while (true) {
        const std::uintptr_t r = dcss_read(a);
        if (!mw::has_flag<kcas_descriptor>(r)) {
            return r;
        }
        std::ignore = kcas_help(r);
    }
}

meta::result<bool, mw::bottom> kcas_help(mw::pointer_type fdes) {
    static constexpr auto read_state = [](mw::pointer_type des) {
        return mw::read_mutables<kcas_descriptor>(des) //
            .map([](mw::state_type mutables) { return mutables & kcas_descriptor::state_mask; });
    };

    const mw::pointer_type des = mw::unset_flag<kcas_descriptor>(fdes);

    const auto initial_state = read_state(des);
    if (!initial_state.has_value()) { // is bottom
        return meta::err(mw::bottom{});
    }

    // Use DCSS to store fdes in each of a1, a2, ... , ak
    // only if des has STATE Undecided and ai = ei for all i
    if (initial_state == kcas_descriptor::state_undecided) {
        mw::state_type state = kcas_descriptor::state_succeded;

        std::size_t i = 0;
        while (true) {
            const auto immutables_result = mw::read_immutables<kcas_descriptor>(des);
            if (!immutables_result.has_value()) { // is bottom
                return meta::err(mw::bottom{});
            }

            const kcas_descriptor::immutables_type& immutables = immutables_result.value();
            if (i >= immutables.k) {
                break;
            }

            detail::atomic<std::uintptr_t>* const a2 = immutables.as[i];
            const std::uintptr_t e2 = immutables.es[i];

            const mw::pointer_type val = detail::dcss(
                des,
                [](mw::pointer_type des) { return read_state(des).value_or(kcas_descriptor::state_succeded); },
                kcas_descriptor::state_undecided,
                a2,
                e2,
                fdes
            );

            if (mw::has_flag<kcas_descriptor>(val)) {
                if (val != fdes) {
                    std::ignore = kcas_help(val);
                    continue;
                }
            } else if (val != e2) {
                state = kcas_descriptor::state_failed;
                break;
            }

            ++i;
        }

        const auto cas_result =
            mw::cas_mutable<kcas_descriptor, kcas_descriptor::state_mask>(des, kcas_descriptor::state_undecided, state);
        if (!cas_result.has_value()) { // is bottom
            return meta::err(mw::bottom{});
        }
    }

    // Replace fdes in a1, ..., ak with n1, ..., nk or e1, ..., ek
    const auto state = read_state(des);
    const bool state_is_succeded = state == kcas_descriptor::state_succeded;

    {
        std::size_t i = 0;
        while (true) {
            const auto immutables_result = mw::read_immutables<kcas_descriptor>(des);
            if (!immutables_result.has_value()) { // is bottom
                return meta::err(mw::bottom{});
            }

            const kcas_descriptor::immutables_type& immutables = immutables_result.value();
            if (i >= immutables.k) {
                break;
            }

            detail::atomic<std::uintptr_t>* const a = immutables.as[i];
            const std::uintptr_t n = state_is_succeded ? immutables.ns[i] : immutables.es[i];
            std::uintptr_t e = fdes;
            std::ignore = a->compare_exchange_strong(e, n, std::memory_order::release, std::memory_order::relaxed);

            ++i;
        }
    }

    return state_is_succeded;
}

} // namespace sl::exec::detail
