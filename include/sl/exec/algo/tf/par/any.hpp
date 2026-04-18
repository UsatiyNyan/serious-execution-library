//
// Created by usatiynyan.
// NOTE: `any(...)` on signals breaks propagation of `try_cancel()`
//

#pragma once

#include "sl/exec/algo/sync/detail/parallel.hpp"
#include "sl/exec/model/concept.hpp"
#include "sl/exec/thread/detail/atomic.hpp"

#include <sl/meta/assert.hpp>
#include <sl/meta/func/lazy_eval.hpp>
#include <sl/meta/type/pack.hpp>

#include <memory>

namespace sl::exec {
namespace detail {

template <typename ValueT, typename ErrorT, template <typename> typename Atomic, typename SlotCtorT, SomeSignal... SignalTs>
struct any_connection final {
    using slot_type = SlotFrom<SlotCtorT>;

private:
    struct any_slot final {
        any_connection& self;
        std::size_t index;

        void set_value(ValueT&& value) && noexcept { self.set_value_impl(index, std::move(value)); }
        void set_error(ErrorT&& error) && noexcept { self.set_error_impl(std::move(error)); }
        void set_null() && noexcept { self.set_null_impl(); }
    };

    struct any_slot_ctor final {
        any_connection& self;
        std::size_t index;

        constexpr any_slot operator()() && noexcept { return any_slot{ self, index }; }
    };

    struct any_delete_this {
        any_connection* this_;
        void operator()() noexcept { delete this_; }
    };

    template <typename SignalT>
    using Connection = ConnectionFor<SignalT, any_slot_ctor>;
    static constexpr std::size_t N = sizeof...(SignalTs);

    template <std::size_t... Indexes>
    static auto
        make_connections(any_connection& self, std::tuple<SignalTs...>&& signals, std::index_sequence<Indexes...>) {
        return std::make_tuple(meta::lazy_eval{ [signal = std::move(std::get<Indexes>(signals)), &self]() mutable {
            return std::move(signal).subscribe(any_slot_ctor{ self, Indexes });
        } }...);
    }

public:
    any_connection(
        std::tuple<SignalTs...>&& signals,
        serial_executor<Atomic>& an_executor,
        SlotCtorT slot_ctor
    )
        : parallel_{ make_connections(*this, std::move(signals), std::make_index_sequence<N>()),
                     an_executor,
                     any_delete_this{ this } },
          slot_{ std::move(slot_ctor)() } {}

public: // connection
    CancelHandle auto emit() && noexcept { return std::move(parallel_).emit(); }

private: // set_...
    void set_value_impl(std::size_t index, ValueT&& value) noexcept {
        if (!done_.exchange(true, std::memory_order::acq_rel)) {
            std::move(slot_).set_value(std::move(value));
            parallel_.schedule_try_cancel_beside(index);
        }

        const bool is_last = parallel_.increment_and_check();
        if (is_last) {
            parallel_.schedule_delete_this();
        }
    }

    void set_error_impl(ErrorT&& error) noexcept {
        const bool is_last = parallel_.increment_and_check();
        if (!is_last) {
            return;
        }

        if (!done_.exchange(true, std::memory_order::acq_rel)) {
            std::move(slot_).set_error(std::move(error));
        }

        parallel_.schedule_delete_this();
    }

    void set_null_impl() noexcept {
        const bool is_last = parallel_.increment_and_check();
        if (!is_last) {
            return;
        }

        if (!done_.exchange(true, std::memory_order::acq_rel)) {
            std::move(slot_).set_null();
        }

        parallel_.schedule_delete_this();
    }

private:
    parallel_connection<any_delete_this, Atomic, Connection<SignalTs>...> parallel_;
    slot_type slot_;
    alignas(hardware_destructive_interference_size) Atomic<bool> done_{ false };
};

template <typename ValueT, typename ErrorT, template <typename> typename Atomic, typename SlotCtorT, SomeSignal... SignalTs>
struct any_connection_box final {
    using connection_type = any_connection<ValueT, ErrorT, Atomic, SlotCtorT, SignalTs...>;

public:
    any_connection_box(
        std::tuple<SignalTs...>&& signals,
        serial_executor<Atomic>& an_executor,
        SlotCtorT slot_ctor
    )
        : connection_{ std::make_unique<connection_type>(std::move(signals), an_executor, std::move(slot_ctor)) } {}

    CancelHandle auto emit() && noexcept {
        auto& a_connection = *DEBUG_ASSERT_VAL(connection_.release());
        return std::move(a_connection).emit();
    }

private:
    std::unique_ptr<connection_type> connection_;
};

template <template <typename> typename Atomic, SomeSignal... SignalTs>
    requires meta::type::are_same_v<typename SignalTs::value_type...>
             && meta::type::are_same_v<typename SignalTs::error_type...>
struct [[nodiscard]] any_signal final {
    using value_type = meta::type::head_t<typename SignalTs::value_type...>;
    using error_type = meta::type::head_t<typename SignalTs::error_type...>;

public:
    explicit any_signal(SignalTs&&... signals, serial_executor<Atomic>& an_executor)
        : signals_{ std::move(signals)... }, executor_{ an_executor } {}

    template <SlotCtor<value_type, error_type> SlotCtorT>
    constexpr Connection auto subscribe(SlotCtorT&& slot_ctor) && noexcept {
        return any_connection_box<value_type, error_type, Atomic, SlotCtorT, SignalTs...>{
            std::move(signals_),
            executor_,
            std::move(slot_ctor),
        };
    }

    executor& get_executor() noexcept { return executor_.get_inner(); }

private:
    std::tuple<SignalTs...> signals_;
    serial_executor<Atomic>& executor_;
};

} // namespace detail

template <template <typename> typename Atomic>
constexpr auto any_(serial_executor<Atomic>& an_executor = detail::inline_serial_executor<Atomic>()) {
    return [&]<SomeSignal... SignalTs>(SignalTs... signals) {
        return detail::any_signal<Atomic, SignalTs...>{ std::move(signals)..., an_executor };
    };
}

template <SomeSignal... SignalTs>
constexpr SomeSignal auto any(SignalTs... signals) {
    return any_<detail::atomic>(detail::inline_serial_executor<detail::atomic>())(std::move(signals)...);
}

} // namespace sl::exec
