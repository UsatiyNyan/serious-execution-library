//
// Created by usatiynyan.
// `flatten()` breaks cancellation chain for a signal created dynamically
//

#pragma once

#include "sl/exec/model/concept.hpp"
#include "sl/exec/model/executor.hpp"

#include <sl/meta/func/lazy_eval.hpp>
#include <sl/meta/monad/maybe.hpp>

namespace sl::exec {
namespace detail {

template <SomeSignal SignalValueT, typename SlotT>
struct [[nodiscard]] flatten_slot final {
    using value_type = typename SignalValueT::value_type;
    using error_type = typename SignalValueT::error_type;

    struct inner_slot {
        SlotT slot;

        void set_value(value_type&& v) && noexcept { std::move(slot).set_value(std::move(v)); }
        void set_error(error_type&& e) && noexcept { std::move(slot).set_error(std::move(e)); }
        void set_null() && noexcept { std::move(slot).set_null(); }
    };

    struct inner_slot_ctor {
        SlotT slot;

        constexpr inner_slot operator()() && noexcept { return inner_slot{ std::move(slot) }; }
    };

    SlotT slot_;
    meta::maybe<ConnectionFor<SignalValueT, inner_slot_ctor>> maybe_connection_{};

    void set_value(SignalValueT&& signal) && noexcept {
        auto& conn = maybe_connection_.emplace(
            meta::lazy_eval{ [&] { return std::move(signal).subscribe(inner_slot_ctor{ std::move(slot_) }); } }
        );
        // need to ignore cancel_handle here, otherwise - race
        std::ignore = std::move(conn).emit();
    }
    void set_error(error_type&& error) && noexcept { std::move(slot_).set_error(std::move(error)); }
    void set_null() && noexcept { std::move(slot_).set_null(); }
};

template <SomeSignal SignalT, typename SlotCtorT, SomeSignal SignalValueT = typename SignalT::value_type>
    requires std::same_as<typename SignalT::error_type, typename SignalValueT::error_type>
struct [[nodiscard]] flatten_connection final {
    using value_type = typename SignalValueT::value_type;
    using error_type = typename SignalT::error_type;
    using SlotT = SlotFrom<SlotCtorT>;
    using flatten_slot_type = flatten_slot<SignalValueT, SlotT>;

    struct flatten_slot_ctor {
        SlotCtorT slot_ctor;

        constexpr flatten_slot_type operator()() && noexcept {
            return flatten_slot_type{ .slot_ = std::move(slot_ctor)() };
        }
    };

    ConnectionFor<SignalT, flatten_slot_ctor> connection;

    constexpr CancelHandle auto emit() && noexcept { return std::move(connection).emit(); }
};

template <SomeSignal SignalT, SomeSignal SignalValueT = typename SignalT::value_type>
    requires std::same_as<typename SignalT::error_type, typename SignalValueT::error_type>
struct [[nodiscard]] flatten_signal final {
    using value_type = typename SignalValueT::value_type;
    using error_type = typename SignalT::error_type;

public:
    constexpr explicit flatten_signal(SignalT&& signal) : signal_{ std::move(signal) } {}

    template <SlotCtor<value_type, error_type> SlotCtorT>
    constexpr Connection auto subscribe(SlotCtorT&& slot_ctor) && noexcept {
        using ConnectionT = flatten_connection<SignalT, SlotCtorT>;
        using SlotCtorForSignal = typename ConnectionT::flatten_slot_ctor;
        return ConnectionT{
            .connection = std::move(signal_).subscribe(SlotCtorForSignal{ std::move(slot_ctor) }),
        };
    }

    constexpr executor& get_executor() noexcept { return signal_.get_executor(); }

private:
    SignalT signal_;
};

struct [[nodiscard]] flatten final {
    template <SomeSignal SignalT>
    constexpr SomeSignal auto operator()(SignalT&& signal) && noexcept {
        return flatten_signal<SignalT>{ std::move(signal) };
    }
};

} // namespace detail

constexpr auto flatten() noexcept { return detail::flatten{}; }

} // namespace sl::exec
