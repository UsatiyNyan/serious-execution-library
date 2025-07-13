//
// Created by usatiynyan.
//
// Based on a paper:
// Maya Arbel-Raviv and Trevor Brown.
// "Reuse, don’t Recycle: Transforming Lock-free Algorithms that Throw Away Descriptors"
//
// No multi word mutables for now.
//
// mutables_type: [ mutable[i]... | sequence ]
//
// TODO:
// - check if all operations can be "relaxed"
//

#pragma once

#include "sl/exec/thread/detail/atomic.hpp"
#include "sl/exec/thread/detail/bits.hpp"
#include "sl/exec/thread/detail/polyfill.hpp"

#include <sl/meta/monad/result.hpp>

#include <libassert/assert.hpp>

#include <bit>
#include <concepts>
#include <cstdint>
#include <utility>

namespace sl::exec::detail::mw {

using state_type = std::uintptr_t;
using pointer_type = std::uintptr_t;
static constexpr std::size_t default_max_threads = SL_EXEC_MW_MAX_THREADS_DEFAULT;

// on 64-bit system:
// [ 63..32 | 31..0 ]
//   mut    | seq
template <typename DT>
struct state_traits {
    static constexpr std::size_t state_width = sizeof(state_type) * CHAR_BIT;
    static constexpr std::size_t mutables_width = (state_width >> 1);
    static constexpr std::size_t sequence_width = (state_width >> 1);
    static constexpr state_type mutables_mask = bits::fill_ones<state_type>(mutables_width);
    static constexpr state_type sequence_mask = bits::fill_ones<state_type>(sequence_width);

public:
    static state_type sequence_inc(state_type sequence) { return (sequence + 1) & sequence_mask; }

    // state -> (mutables, sequence)
    static std::pair<state_type, state_type> extract(state_type state) {
        return { (state >> sequence_width) & mutables_mask, state & sequence_mask };
    }
    // (mutables, sequence) -> state
    static state_type combine(state_type mutables, state_type sequence) {
        return ((mutables & mutables_mask) << sequence_width) | (sequence & sequence_mask);
    }
};

// on 64-bit system:
// [ 63..48 | 47..32 | 31..0 ]
//   flag   | pid    | seq
template <typename DT>
struct pointer_traits {
    static constexpr std::size_t ptr_width = sizeof(pointer_type) * CHAR_BIT;
    static constexpr std::size_t sequence_width = (ptr_width >> 1);
    static constexpr std::size_t pid_width = (ptr_width >> 2);
    static constexpr std::size_t flag_width = (ptr_width >> 2);
    static constexpr pointer_type sequence_mask = bits::fill_ones<pointer_type>(sequence_width);
    static constexpr pointer_type pid_mask = bits::fill_ones<pointer_type>(pid_width);
    static constexpr pointer_type flag_mask = bits::fill_ones<pointer_type>(flag_width);

    static_assert(ptr_width == state_traits<DT>::state_width);
    static_assert(sequence_width == state_traits<DT>::sequence_width);
    static_assert(sequence_mask == state_traits<DT>::sequence_mask);

public:
    // pointer -> (flag, pid, sequence)
    static std::tuple<pointer_type, pointer_type, pointer_type> extract(pointer_type pointer) {
        return {
            (pointer >> sequence_width >> pid_width) & flag_mask,
            (pointer >> sequence_width) & pid_mask,
            pointer & sequence_mask,
        };
    }
    // (flag, pid, sequence) -> pointer
    static pointer_type combine(pointer_type flag, pointer_type pid, pointer_type sequence) {
        return ((flag & flag_mask) << sequence_width << pid_width) | //
               ((pid & pid_mask) << sequence_width) | //
               (sequence & sequence_mask);
    }
};

// Descriptor of type T :
// mutables = <seq, mut1, mut2, ...>     |> Mutable fields (renamed to state to avoid confusion)
// imm1, imm2, ...                       |> Immutable fields
template <typename DT>
concept Descriptor = requires(DT descriptor) {
    { DT::max_threads } -> std::same_as<const pointer_type&>;
    typename DT::template atomic_type<state_type>;
    { descriptor.state } -> std::same_as<typename DT::template atomic_type<state_type>&>;
    typename DT::immutables_type;
    { descriptor.immutables } -> std::same_as<typename DT::immutables_type&>;
};

// special type
struct bottom {};

template <Descriptor DT, state_type FieldMask>
static constexpr bool valid_field_mask =
    (FieldMask != 0) && (std::bit_width(FieldMask) <= state_traits<DT>::mutables_width);

// D{T,p} for each descriptor type T and process p
// immitating 'thread_local' behaviour
template <Descriptor DT>
    requires(std::bit_width(DT::max_threads) <= pointer_traits<DT>::pid_mask)
struct descriptor_pool {
    static constexpr std::size_t max_threads = DT::max_threads;

    struct alignas(hardware_destructive_interference_size) padded {
        DT value;
    };

    using descriptors_type = std::array<padded, max_threads>;

public:
    static pointer_type make_pid() {
        static typename DT::template atomic_type<pointer_type> counter = 0;
        thread_local const std::size_t pid = counter.fetch_add(1, std::memory_order::relaxed);
        DEBUG_ASSERT(pid < max_threads);
        return pid;
    }

    static DT& get(pointer_type pid) {
        DEBUG_ASSERT(pid < max_threads);
        return descriptors[pid].value;
    }

public:
    static descriptors_type descriptors;
};

#define SL_EXEC_DESCRIPTOR_POOL_TEMPLATE_INSTANTIATE(T)          \
    using T##_pool = ::sl::exec::detail::mw::descriptor_pool<T>; \
    template <>                                                  \
    T##_pool::descriptors_type T##_pool::descriptors{};

// CreateNew(T, v1, v2, ...) by process p :
//   oldseq := D{T,p}.mutables.seq
//   D{T,p}.mutables.seq := oldseq + 1
//   for each field f in D{T,p}
//     let value be the corresponding value in {v1, v2, ...}
//     if f is immutable then
//       D{T,p}.f := value
//     else
//       D{T,p}.mutables.f := value
//   D{T,p}.mutables.seq := oldseq + 2
//   return <p, oldseq + 2>
//
// ---
// atomics ordering:
// sequence load, and first increment are "relaxed" - since this thread is the only one able to write to immutables
// last increment is "release" - write to immutables has to be observable by other threads afterwards
template <Descriptor DT>
[[nodiscard]] pointer_type create_new(state_type mutables, typename DT::immutables_type immutables) {
    using state_impl = state_traits<DT>;
    using pointer_impl = pointer_traits<DT>;

    const pointer_type pid = descriptor_pool<DT>::make_pid();
    DT& descriptor = descriptor_pool<DT>::get(pid);

    // oldseq := D{T,p}.mutables.seq
    const state_type old_state = descriptor.state.load(std::memory_order::relaxed);
    const auto [old_mutables, old_sequence] = state_impl::extract(old_state);

    // D{T,p}.mutables.seq := oldseq + 1
    const state_type old_sequence_1 = state_impl::sequence_inc(old_sequence);
    descriptor.state.store(state_impl::combine(old_mutables, old_sequence_1), std::memory_order::relaxed);

    // for each field f in D{T,p}
    //   let value be the corresponding value in {v1, v2, ...}
    //   if f is immutable then
    //     D{T,p}.f := value
    //   else
    //     D{T,p}.mutables.f := value
    descriptor.immutables = immutables;

    // D{T,p}.mutables.seq := oldseq + 2
    const state_type old_sequence_2 = state_impl::sequence_inc(old_sequence_1);
    descriptor.state.store(state_impl::combine(mutables, old_sequence_2), std::memory_order::release);

    // return <p, oldseq + 2>
    return pointer_impl::combine(/* flag = */ pointer_type{}, pid, old_sequence_2);
}

// ReadField(des, f, dv) :
//   <q, seq> := des
//   if f is immutable then
//     result := D{T,q}.f
//   else
//     result := D{T,q}.mutables.f
//   if seq != D{T,q}.mutables.seq then return dv
//   return result
//
// ---
// algorithm was changed to read mutables only, and returning bottom for consistency, dv can be reproduced with value_or
// atomics ordering:
// sequence load is acquire - could be relaxed, but read_mutables may be used in "message-passing" context
template <Descriptor DT>
[[nodiscard]] meta::result<state_type, bottom> read_mutables(pointer_type pointer) {
    using state_impl = state_traits<DT>;
    using pointer_impl = pointer_traits<DT>;

    // <q, seq> := des
    auto [flag, pid, sequence] = pointer_impl::extract(pointer);
    DT& descriptor = descriptor_pool<DT>::get(pid);

    // result := D{T,q}.mutables.f
    const state_type state = descriptor.state.load(std::memory_order::acquire);
    const auto [mutables, state_sequence] = state_impl::extract(state);

    // if seq != D{T,q}.mutables.seq then return dv
    if (sequence != state_sequence) {
        return meta::err(bottom{});
    }

    return mutables;
}

// ReadImmutables(des) :
//   <q, seq> := des
//   for each f in des
//     if f is immutable then add D{T,q}.f to result
//   if seq != D{T,q}.mutables.seq then return ⊥
//   return result
//
// ---
// atomics ordering:
// mutables load is acquire - writes to immutables has to be observable,
//                            may need to be removed if it is confirmed to be "wasteful"
// last load is relaxed - only important for sequence validation according to initial algorithm
template <Descriptor DT>
[[nodiscard]] meta::result<typename DT::immutables_type, bottom> read_immutables(pointer_type pointer) {
    using state_impl = state_traits<DT>;
    using pointer_impl = pointer_traits<DT>;

    // <q, seq> := des
    auto [flag, pid, sequence] = pointer_impl::extract(pointer);
    DT& descriptor = descriptor_pool<DT>::get(pid);

    {
        // this is not in the initial algorithm, mainly here for acquire semantics
        const state_type state = descriptor.state.load(std::memory_order::acquire);
        const auto [mutables, state_sequence] = state_impl::extract(state);
        if (sequence != state_sequence) {
            return meta::err(bottom{});
        }
    }

    // for each f in des
    //   if f is immutable then add D{T,q}.f to result
    typename DT::immutables_type result = descriptor.immutables;

    {
        // if seq != D{T,q}.mutables.seq then return ⊥
        const state_type state = descriptor.state.load(std::memory_order::relaxed);
        const auto [mutables, state_sequence] = state_impl::extract(state);
        if (sequence != state_sequence) {
            return meta::err(bottom{});
        }
    }

    return result;
}

// WriteField(des, f, value) :
//   <q, seq> := des
//   loop
//     exp := D{T,q}.mutables
//     if exp.seq != seq then return
//     new := exp
//     new.f := value
//     if CAS(&D{T,q}.mutables, exp, new) then return
//
// ---
// atomics ordering:
// sequence load is relaxed - no immutables synchronisation needed
// mutables successful store is release - could be relaxed, but write_mutable could be used in "message-passing" context
template <Descriptor DT, state_type FieldMask>
    requires valid_field_mask<DT, FieldMask>
void write_mutable(pointer_type pointer, state_type field_value) {
    using state_impl = state_traits<DT>;
    using pointer_impl = pointer_traits<DT>;

    DEBUG_ASSERT((field_value & ~FieldMask) == 0, "should not have value outside of mask");

    // <q, seq> := des
    auto [flag, pid, sequence] = pointer_impl::extract(pointer);
    DT& descriptor = descriptor_pool<DT>::get(pid);

    // loop
    while (true) {
        // exp := D{T,q}.mutables
        state_type expected_state = descriptor.state.load(std::memory_order::relaxed);
        const auto [expected_mutables, expected_sequence] = state_impl::extract(expected_state);

        // if exp.seq != seq then return
        if (sequence != expected_sequence) {
            return;
        }

        // new := exp
        // new.f := value
        const state_type new_mutables = (expected_mutables & ~FieldMask) | field_value;
        const state_type new_state = state_impl::combine(new_mutables, expected_sequence);

        // if CAS(&D{T,q}.mutables, exp, new) then return
        if (descriptor.state.compare_exchange_weak(
                expected_state, new_state, std::memory_order::release, std::memory_order::relaxed
            )) {
            return;
        }
    }
}

// CASField(des, f, fexp, fnew) :
//   <q, seq> := des
//   loop
//     exp := D{T,q}.mutables
//     if exp.seq != seq then return ⊥
//     if exp.f != fexp then return exp.f
//     new := exp
//     new.f := fnew
//     if CAS(&D{T,q}.mutables, exp, new) then
//       return fnew
//
// ---
// atomics ordering:
// sequence and mutables load is acquire - sequence could be relaxed, but mutables may participate in "message-passing"
// mutables write is release - similarly, mutables may be used in "message-passing" context
// basically, it's similar to `mutables.compare_exchange(e, n, mo::release, mo::acquire)`
template <Descriptor DT, state_type FieldMask>
    requires valid_field_mask<DT, FieldMask>
[[nodiscard]] meta::result<state_type, bottom>
    cas_mutable(pointer_type pointer, state_type expected_value, state_type new_value) {
    using state_impl = state_traits<DT>;
    using pointer_impl = pointer_traits<DT>;

    DEBUG_ASSERT((expected_value & ~FieldMask) == 0, "should not have value outside of mask");
    DEBUG_ASSERT((new_value & ~FieldMask) == 0, "should not have value outside of mask");

    // <q, seq> := des
    auto [flag, pid, sequence] = pointer_impl::extract(pointer);
    DT& descriptor = descriptor_pool<DT>::get(pid);

    // loop
    while (true) {
        // exp := D{T,q}.mutables
        state_type expected_state = descriptor.state.load(std::memory_order::relaxed);
        const auto [expected_mutables, expected_sequence] = state_impl::extract(expected_state);

        // if exp.seq != seq then return ⊥
        if (sequence != expected_sequence) {
            return meta::err(bottom{});
        }

        // if exp.f != fexp then return exp.f
        const state_type expected_mutables_field = expected_mutables & FieldMask;
        if (expected_mutables_field != expected_value) {
            return expected_mutables_field;
        }

        // new := exp
        // new.f := fnew
        const state_type new_mutables = (expected_mutables & ~FieldMask) | new_value;
        const state_type new_state = state_impl::combine(new_mutables, expected_sequence);

        // if CAS(&D{T,q}.mutables, exp, new) then
        //   return fnew
        if (descriptor.state.compare_exchange_weak(
                expected_state, new_state, std::memory_order::release, std::memory_order::relaxed
            )) {
            return new_value;
        }
    }
}

template <typename DT>
concept DescriptorWithFlag = Descriptor<DT> && std::has_single_bit(DT::flag_bit);

template <DescriptorWithFlag DT>
bool has_flag(mw::pointer_type des) {
    const auto [flag, pid, seq] = pointer_traits<DT>::extract(des);
    return (flag & DT::flag_bit) == DT::flag_bit;
}

template <DescriptorWithFlag DT>
mw::pointer_type toggle_flag(mw::pointer_type des) {
    using pointer_impl = pointer_traits<DT>;
    const auto [flag, pid, seq] = pointer_impl::extract(des);
    const mw::pointer_type new_flag = flag ^ DT::flag_bit;
    return pointer_impl::combine(new_flag, pid, seq);
}

template <DescriptorWithFlag DT>
mw::pointer_type set_flag(mw::pointer_type des) {
    DEBUG_ASSERT(!has_flag<DT>(des));
    return toggle_flag<DT>(des);
}

template <DescriptorWithFlag DT>
mw::pointer_type unset_flag(mw::pointer_type fdes) {
    DEBUG_ASSERT(has_flag<DT>(fdes));
    return toggle_flag<DT>(fdes);
}

} // namespace sl::exec::detail::mw
