//
// Created by usatiynyan.
// Based on a paper:
// Maya Arbel-Raviv and Trevor Brown.
// "Reuse, don’t Recycle: Transforming Lock-free Algorithms that Throw Away Descriptors"
//

#pragma once

#include "sl/exec/thread/detail/multiword.hpp"

namespace sl::exec {
namespace detail {

struct dcss_descriptor {
    // concept requirements
    static constexpr std::size_t max_threads = mw::default_max_threads;

    template <typename T>
    using atomic_type = detail::atomic<T>;

    struct immutables_type {
        detail::atomic<std::uintptr_t>* a1{};
        std::uintptr_t e1{};
        detail::atomic<std::uintptr_t>* a2{};
        std::uintptr_t e2{};
        std::uintptr_t n2{};

        bool operator==(const immutables_type&) const = default;
    };

public:
    detail::atomic<mw::state_type> state{ 0 };
    immutables_type immutables{};
};

std::uintptr_t dcss(
    detail::atomic<std::uintptr_t>* a1,
    std::uintptr_t e1,
    detail::atomic<std::uintptr_t>* a2,
    std::uintptr_t e2,
    std::uintptr_t n2
);

std::uintptr_t dcss_read(detail::atomic<std::uintptr_t>* a);

void dcss_help(mw::pointer_type fdes);

bool dcss_check_flag(mw::pointer_type des);
mw::pointer_type dcss_set_flag(mw::pointer_type des, bool flag_value);

} // namespace detail

// (a1, e1, a2, e2, n2)
//   -> e2 if succeeded
//   -> curr value of a2 if failed
template <typename T>
    requires(sizeof(T) == sizeof(std::uintptr_t))
T dcss(detail::atomic<T>& a1, T e1, detail::atomic<T>& a2, T e2, T n2) {
    std::uintptr_t result = detail::dcss(
        std::bit_cast<detail::atomic<std::uintptr_t>*>(&a1),
        std::bit_cast<std::uintptr_t>(e1),
        std::bit_cast<detail::atomic<std::uintptr_t>*>(&a2),
        std::bit_cast<std::uintptr_t>(e2),
        std::bit_cast<std::uintptr_t>(n2)
    );
    return std::bit_cast<T>(result);
}

} // namespace sl::exec
