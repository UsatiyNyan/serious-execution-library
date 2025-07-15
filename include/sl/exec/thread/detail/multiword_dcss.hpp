//
// Created by usatiynyan.
// Based on a paper:
// Maya Arbel-Raviv and Trevor Brown.
// "Reuse, don’t Recycle: Transforming Lock-free Algorithms that Throw Away Descriptors"
//

#pragma once

#include "sl/exec/thread/detail/multiword.hpp"

#include <cstdint>

namespace sl::exec::detail {

// (std::uintptr_t) -> uintptr_t
using dcss_a1_load_type = std::uintptr_t (*)(std::uintptr_t);

template <typename T>
std::uintptr_t dcss_a1_load_default(std::uintptr_t a1_erased) {
    auto* a1 = std::bit_cast<detail::atomic<T>*>(a1_erased);
    const auto result = a1->load(std::memory_order::relaxed);
    return std::bit_cast<std::uintptr_t>(result);
}

struct dcss_descriptor {
    // concept requirements
    static constexpr std::size_t max_threads = mw::default_max_threads;

    template <typename T>
    using atomic_type = detail::atomic<T>;

    struct immutables_type {
        std::uintptr_t a1{};
        dcss_a1_load_type a1_load{};
        std::uintptr_t e1{};
        detail::atomic<std::uintptr_t>* a2{};
        std::uintptr_t e2{};
        std::uintptr_t n2{};
    };

    // choose highest flag bit
    static constexpr mw::pointer_type flag_bit = 1 << (mw::pointer_traits<dcss_descriptor>::flag_width - 1);

public:
    detail::atomic<mw::state_type> state{ 0 };
    immutables_type immutables{};
};

mw::pointer_type dcss_create_new(
    std::uintptr_t a1,
    dcss_a1_load_type a1_load,
    std::uintptr_t e1,
    detail::atomic<std::uintptr_t>* a2,
    std::uintptr_t e2,
    std::uintptr_t n2
);
std::uintptr_t dcss(
    std::uintptr_t a1,
    dcss_a1_load_type a1_load,
    std::uintptr_t e1,
    detail::atomic<std::uintptr_t>* a2,
    std::uintptr_t e2,
    std::uintptr_t n2
);
std::uintptr_t dcss_read(detail::atomic<std::uintptr_t>* a);
void dcss_help(mw::pointer_type fdes);

// "public":

// (a1, e1, a2, e2, n2)
//   -> e2 if succeeded
//   -> curr value of a2 if failed
template <typename T>
    requires(sizeof(T) == sizeof(std::uintptr_t))
T dcss(detail::atomic<T>& a1, T e1, detail::atomic<T>& a2, T e2, T n2) {
    std::uintptr_t result = detail::dcss(
        std::bit_cast<std::uintptr_t>(&a1),
        dcss_a1_load_default<T>,
        std::bit_cast<std::uintptr_t>(e1),
        std::bit_cast<detail::atomic<std::uintptr_t>*>(&a2),
        std::bit_cast<std::uintptr_t>(e2),
        std::bit_cast<std::uintptr_t>(n2)
    );
    return std::bit_cast<T>(result);
}

// (&a) -> *a
// value of any atomic that was used in dcss should be acquired using dcss_read
template <typename T>
    requires(sizeof(T) == sizeof(std::uintptr_t))
T dcss_read(detail::atomic<T>& a) {
    const std::uintptr_t result = detail::dcss_read(std::bit_cast<detail::atomic<std::uintptr_t>*>(&a));
    return std::bit_cast<T>(result);
}

} // namespace sl::exec::detail
