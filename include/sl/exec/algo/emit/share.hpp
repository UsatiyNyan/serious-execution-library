//
// Created by usatiynyan.
// usage:
//  - create via `share_box{std::move(signal)}` or `| share()`
//  - inherit internal result via `share_box::get_signal()`
// Signals created this way do not inherit executor, they continue inline.
// Is lazy until first subscription emit-s.
//

#pragma once

#include "sl/exec/model/concept.hpp"
#include "sl/exec/model/slot.hpp"

#include "sl/exec/thread/detail/atomic.hpp"
#include "sl/exec/thread/detail/polyfill.hpp"

#include <sl/meta/assert.hpp>
#include <sl/meta/intrusive/algorithm.hpp>
#include <sl/meta/lifetime/finalizer.hpp>

#include <bit>

namespace sl::exec {
namespace detail {

template <typename ValueT, typename ErrorT>
struct share_callback : meta::intrusive_forward_list_node<share_callback<ValueT, ErrorT>> {
    virtual ~share_callback() = default;
    virtual void operator()(meta::maybe<meta::result<ValueT, ErrorT>>&& result) && noexcept = 0;
};

template <typename ValueT, typename ErrorT, template <typename> typename Atomic>
struct [[nodiscard]] share_storage_base {
    using value_type = ValueT;
    using error_type = ErrorT;
    using result_type = meta::result<value_type, error_type>;

    enum state : std::uintptr_t {
        state_empty = std::numeric_limits<std::uintptr_t>::min(),
        state_result = std::numeric_limits<std::uintptr_t>::max(),
    };

    struct slot {
        void set_value(value_type&& value) && noexcept {
            self.set_result(result_type{ meta::ok_tag, std::move(value) });
        }
        void set_error(error_type&& error) && noexcept {
            self.set_result(result_type{ meta::err_tag, std::move(error) });
        }
        void set_null() && noexcept { self.set_result(meta::null); }

    public:
        share_storage_base& self;
    };

    struct slot_ctor {
        constexpr slot operator()() && noexcept { return slot{ self }; }

    public:
        share_storage_base& self;
    };

public: // type erasure
    virtual ~share_storage_base() = default;

protected:
    virtual void internal_connection_emit() & = 0;

public: // refcount
    constexpr explicit share_storage_base(std::uint32_t refcount) : refcount_{ refcount } {}

    std::uint32_t incref(std::uint32_t diff = 1) & {
        const std::uint32_t prev = refcount_.fetch_add(diff, std::memory_order::relaxed);
        return prev + diff;
    }

    std::uint32_t decref(std::uint32_t diff = 1) & {
        const std::uint32_t prev = refcount_.fetch_sub(diff, std::memory_order::acq_rel);
        if (prev == diff) {
            delete this;
        }
        return prev - diff;
    }

    static void try_decref(share_storage_base* storage_ptr) {
        if (nullptr != storage_ptr) {
            storage_ptr->decref();
        }
    }

public: // fulfillment
    void emit(share_callback<ValueT, ErrorT>& a_callback) & {
        std::uintptr_t curr_state = state_empty;
        if (state_.compare_exchange_strong(
                curr_state,
                std::bit_cast<std::uintptr_t>(&a_callback),
                std::memory_order::release,
                std::memory_order::acquire
            )) {
            internal_connection_emit();
            return;
        }

        while (curr_state != state_result) {
            a_callback.intrusive_next = std::bit_cast<share_callback<value_type, error_type>*>(curr_state);
            if (state_.compare_exchange_weak( //
                    curr_state,
                    std::bit_cast<std::uintptr_t>(&a_callback),
                    std::memory_order::release,
                    std::memory_order::acquire
                )) {
                break;
            }
        }

        if (curr_state == state_result) {
            // explicit copy, unfortunately
            std::move(a_callback)(meta::maybe<result_type>{ maybe_result_ });
            decref();
        }
    }

private:
    void set_result(meta::maybe<result_type> maybe_result) & {
        maybe_result_ = std::move(maybe_result);

        std::uintptr_t curr_state = state_.exchange(state_result, std::memory_order::acq_rel);
        const bool state_is_empty = curr_state == state_empty;
        ASSERT(!state_is_empty, "set_result before emit, since this is lazy impl");
        if (state_is_empty) {
            return;
        }
        DEBUG_ASSERT(curr_state != state_result);

        auto* callback_list = std::bit_cast<share_callback<value_type, error_type>*>(curr_state);

        // if any exceptions are thrown, probably everything breaks here
        std::uint32_t callback_count = 0;

        meta::intrusive_forward_list_node_for_each(
            callback_list,
            [&callback_count, &maybe_result_ref = maybe_result_](share_callback<value_type, error_type>* callback_ptr) {
                // explicit copy, unfortunately
                std::move (*callback_ptr)(meta::maybe<result_type>{ maybe_result_ref });
                ++callback_count;
            }
        );

        decref(callback_count);
    }

private:
    meta::maybe<result_type> maybe_result_{};
    alignas(hardware_destructive_interference_size) Atomic<std::uint32_t> refcount_{ 0 };
    alignas(hardware_destructive_interference_size) Atomic<std::uintptr_t> state_{ state_empty };
};

template <
    SomeSignal SignalT,
    template <typename> typename Atomic,
    typename ValueT = typename SignalT::value_type,
    typename ErrorT = typename SignalT::error_type>
struct [[nodiscard]] share_storage final : share_storage_base<ValueT, ErrorT, Atomic> {
    using base_type = share_storage_base<ValueT, ErrorT, Atomic>;
    using slot_ctor_type = typename base_type::slot_ctor;

public:
    constexpr share_storage(SignalT&& signal, std::uint32_t refcount)
        : base_type{ refcount }, connection_{ std::move(signal).subscribe(slot_ctor_type{ *this }) } {}

protected:
    void internal_connection_emit() & override { std::ignore = std::move(connection_).emit(); }

private:
    ConnectionFor<SignalT, slot_ctor_type> connection_;
};

template <typename ValueT, typename ErrorT, template <typename> typename Atomic, typename SlotCtorT>
struct [[nodiscard]] share_connection final : share_callback<ValueT, ErrorT> {
    constexpr share_connection(SlotCtorT&& slot_ctor, share_storage_base<ValueT, ErrorT, Atomic>* storage_ptr)
        : slot_{ std::move(slot_ctor)() }, storage_ptr_{ storage_ptr } {
        // implicitly not propagating cancel-s into original signal
    }

    ~share_connection() override { share_storage_base<ValueT, ErrorT, Atomic>::try_decref(storage_ptr_); }

    CancelHandle auto emit() && noexcept {
        auto* storage_ptr = std::exchange(storage_ptr_, nullptr);
        DEBUG_ASSERT(nullptr != storage_ptr);
        storage_ptr->emit(*this);
        return dummy_cancel_handle{};
    }

    void operator()(meta::maybe<meta::result<ValueT, ErrorT>>&& maybe_result) && noexcept override {
        fulfill_slot(std::move(slot_), std::move(maybe_result));
    }

private:
    SlotFrom<SlotCtorT> slot_;
    share_storage_base<ValueT, ErrorT, Atomic>* storage_ptr_;
};

template <typename ValueT, typename ErrorT, template <typename> typename Atomic>
struct [[nodiscard]] share_signal final : meta::finalizer<share_signal<ValueT, ErrorT, Atomic>> {
    using value_type = ValueT;
    using error_type = ErrorT;

public:
    constexpr explicit share_signal(share_storage_base<value_type, error_type, Atomic>* storage_ptr)
        : meta::finalizer<share_signal>{ [](share_signal& self) {
              share_storage_base<value_type, error_type, Atomic>::try_decref(self.storage_ptr_);
          } },
          storage_ptr_{ storage_ptr } {
        [[maybe_unused]] const std::uint32_t refcount = storage_ptr_->incref();
        DEBUG_ASSERT(refcount > 1u);
    }

    template <SlotCtor<value_type, error_type> SlotCtorT>
    constexpr Connection auto subscribe(SlotCtorT&& slot_ctor) && noexcept {
        auto* storage_ptr = std::exchange(storage_ptr_, nullptr);
        DEBUG_ASSERT(nullptr != storage_ptr);
        return share_connection<value_type, error_type, Atomic, SlotCtorT>{ std::move(slot_ctor), storage_ptr };
    }

    executor& get_executor() & noexcept { return exec::inline_executor(); }

private:
    share_storage_base<value_type, error_type, Atomic>* storage_ptr_;
};

} // namespace detail

template <typename ValueT, typename ErrorT, template <typename> typename Atomic = detail::atomic>
struct [[nodiscard]] share_box : meta::finalizer<share_box<ValueT, ErrorT, Atomic>> {

    template <SomeSignal SignalT>
    constexpr explicit share_box(SignalT&& signal)
        : meta::finalizer<share_box>{ [](share_box& self) { self.storage_ptr_->decref(); } },
          storage_ptr_{ new detail::share_storage<SignalT, Atomic>{
              std::move(signal),
              /* .refcount = */ 1u,
          } } {}

    constexpr Signal<ValueT, ErrorT> auto get_signal() & {
        return detail::share_signal<ValueT, ErrorT, Atomic>{ storage_ptr_ };
    }

private:
    detail::share_storage_base<ValueT, ErrorT, Atomic>* storage_ptr_;
};

namespace detail {

template <template <typename> typename Atomic>
struct [[nodiscard]] share_emit {
    template <SomeSignal SignalT>
    constexpr auto operator()(SignalT&& signal) && {
        return exec::share_box<typename SignalT::value_type, typename SignalT::error_type, Atomic>{ std::move(signal) };
    }
};

} // namespace detail

template <template <typename> typename Atomic = detail::atomic>
constexpr auto share() {
    return detail::share_emit<Atomic>{};
}

} // namespace sl::exec
