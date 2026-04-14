//
// Created by usatiynyan.
// NOTE: `force()` on a signal breaks propagation of `try_cancel()`
//

#pragma once

#include "sl/exec/model/concept.hpp"
#include "sl/exec/model/slot.hpp"
#include "sl/exec/thread/detail/atomic.hpp"

#include <bit>
#include <memory>

namespace sl::exec {
namespace detail {

template <SomeSignal SignalT, template <typename> typename Atomic>
struct force_storage {
    using value_type = typename SignalT::value_type;
    using error_type = typename SignalT::error_type;
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
        force_storage& self;
    };

    struct slot_ctor {
        constexpr slot operator()() && noexcept { return slot{ self }; }

    public:
        force_storage& self;
    };

    using callback_type = slot_callback<value_type, error_type>;

public:
    explicit force_storage(SignalT&& signal) : connection_{ std::move(signal).subscribe(slot_ctor{ *this }) } {
        // if the signal is "forced" it loses it's ability to be cancelled
        // otherwise - it's hard to keep track of lifetimes
        std::ignore = std::move(connection_).emit();
    }

    void set_result(meta::maybe<result_type> result) {
        maybe_result_ = std::move(result);

        std::uintptr_t curr_state = state_.exchange(state_result, std::memory_order::acq_rel);
        if (curr_state == state_empty) {
            return;
        }
        DEBUG_ASSERT(curr_state != state_result);
        auto* a_slot_callback = std::bit_cast<callback_type*>(curr_state);

        std::move(*a_slot_callback).set_result(std::move(maybe_result_));
        delete this;
    }

    void set_slot_callback(callback_type& a_slot_callback) {
        std::uintptr_t expected_state = state_empty;
        if (state_.load(std::memory_order::acquire) == expected_state
            && state_.compare_exchange_strong(
                expected_state,
                std::bit_cast<std::uintptr_t>(&a_slot_callback),
                std::memory_order::release,
                std::memory_order::acquire
            )) {
            return;
        }

        std::move(a_slot_callback).set_result(std::move(maybe_result_));
        delete this;
    }

private:
    ConnectionFor<SignalT, slot_ctor> connection_;
    meta::maybe<result_type> maybe_result_;
    Atomic<std::uintptr_t> state_{ state_empty };
};

template <SomeSignal SignalT, SlotCtorFor<SignalT> SlotCtorT, template <typename> typename Atomic>
struct force_connection final : slot_callback<typename SignalT::value_type, typename SignalT::error_type> {
    force_connection(SlotCtorT&& slot_ctor, std::unique_ptr<force_storage<SignalT, Atomic>> storage)
        : slot_{ std::move(slot_ctor)() }, storage_{ std::move(storage) } {}

    CancelHandle auto emit() && noexcept {
        storage_.release()->set_slot_callback(*this);
        return dummy_cancel_handle();
    }

    void set_result(meta::maybe<ForSignal<meta::result, SignalT>>&& maybe_result) && noexcept override {
        fulfill_slot(std::move(slot_), std::move(maybe_result));
    }

private:
    SlotFrom<SlotCtorT> slot_;
    std::unique_ptr<force_storage<SignalT, Atomic>> storage_;
};

template <SomeSignal SignalT, template <typename> typename Atomic>
struct [[nodiscard]] force_signal final {
    using value_type = typename SignalT::value_type;
    using error_type = typename SignalT::error_type;

public:
    constexpr explicit force_signal(SignalT&& signal)
        : executor_{ &signal.get_executor() }, storage_{ new force_storage<SignalT, Atomic>{ std::move(signal) } } {}

    template <SlotCtorFor<SignalT> SlotCtorT>
    Connection auto subscribe(SlotCtorT&& slot_ctor) && noexcept {
        return force_connection<SignalT, SlotCtorT, Atomic>{ std::move(slot_ctor), std::move(storage_) };
    }

    executor& get_executor() & noexcept { return *executor_; }

private:
    executor* executor_;
    std::unique_ptr<force_storage<SignalT, Atomic>> storage_;
};

template <template <typename> typename Atomic>
struct [[nodiscard]] force final {
    template <SomeSignal SignalT>
    constexpr SomeSignal auto operator()(SignalT&& signal) && {
        return force_signal<SignalT, Atomic>{
            /* .signal = */ std::move(signal),
        };
    }
};

} // namespace detail

template <template <typename> typename Atomic = detail::atomic>
constexpr auto force() {
    return detail::force<Atomic>{};
}

} // namespace sl::exec
