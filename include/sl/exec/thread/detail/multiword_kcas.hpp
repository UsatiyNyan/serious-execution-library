//
// Created by usatiynyan.
// Based on a paper:
// Maya Arbel-Raviv and Trevor Brown.
// "Reuse, don’t Recycle: Transforming Lock-free Algorithms that Throw Away Descriptors"
//

#pragma once

#include "sl/exec/thread/detail/atomic.hpp"
#include "sl/exec/thread/detail/multiword.hpp"
#include "sl/exec/thread/detail/multiword_dcss.hpp"

#include <array>
#include <cstdint>

namespace sl::exec::detail {

struct kcas_descriptor {
    // concept requirements
    static constexpr std::size_t max_threads = mw::default_max_threads;

    template <typename T>
    using atomic_type = detail::atomic<T>;

    struct immutables_type {
        static constexpr std::size_t k_max = SL_EXEC_KCAS_MAX;

        std::array<detail::atomic<std::uintptr_t>*, k_max> as{};
        std::array<std::uintptr_t, k_max> es{};
        std::array<std::uintptr_t, k_max> ns{};
        std::size_t k{};
    };

    // choose second highest flag bit
    static constexpr mw::pointer_type flag_bit = dcss_descriptor::flag_bit >> 1;

    // impl
    static constexpr mw::state_type state_mask = 0b11;
    static constexpr mw::state_type state_undecided = 0b00;
    static constexpr mw::state_type state_succeded = 0b01;
    static constexpr mw::state_type state_failed = 0b10;

public:
    detail::atomic<mw::state_type> state{ 0 };
    immutables_type immutables{};
};

[[nodiscard]] bool kcas(kcas_descriptor::immutables_type immutables);
[[nodiscard]] std::uintptr_t kcas_read(detail::atomic<std::uintptr_t>* a);
[[nodiscard]] meta::result<bool, mw::bottom> kcas_help(mw::pointer_type fdes);

// "public":

template <typename KCasOperandT>
concept KCasOperand = sizeof(KCasOperandT) == sizeof(std::uintptr_t);

template <KCasOperand T>
struct kcas_arg {
    detail::atomic<T>& a;
    T e;
    T n;
};

template <std::size_t K, KCasOperand T>
    requires(K <= kcas_descriptor::immutables_type::k_max)
[[nodiscard]] bool kcas(std::array<kcas_arg<T>, K> args) {
    kcas_descriptor::immutables_type immutables{ .k = K };

    for (std::size_t i = 0; i < K; ++i) {
        kcas_arg<T>& arg = args[i];
        immutables.as[i] = std::bit_cast<detail::atomic<std::uintptr_t>*>(&arg.a);
        const auto e = std::bit_cast<std::uintptr_t>(arg.e);
        immutables.es[i] = e;
        const auto n = std::bit_cast<std::uintptr_t>(arg.n);
        immutables.ns[i] = n;
        DEBUG_ASSERT(
            !mw::has_flag<kcas_descriptor>(e) && !mw::has_flag<kcas_descriptor>(n),
            "algo wouldn't work with second highest bit set in application values"
        );
    }

    return detail::kcas(immutables);
}

template <typename... Args>
[[nodiscard]] bool kcas(Args... args) {
    return kcas<sizeof...(Args)>(std::array{ args... });
}

template <KCasOperand T>
[[nodiscard]] T kcas_read(detail::atomic<T>& a) {
    const std::uintptr_t result = detail::kcas_read(std::bit_cast<detail::atomic<std::uintptr_t>*>(&a));
    return std::bit_cast<T>(result);
}

} // namespace sl::exec::detail
