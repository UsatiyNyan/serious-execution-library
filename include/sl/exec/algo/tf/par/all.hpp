//
// Created by usatiynyan.
// NOTE: `all(...)` on signals breaks propagation of `try_cancel()`
//

#pragma once

#include "sl/exec/algo/sync/detail/parallel.hpp"
#include "sl/exec/model/concept.hpp"
#include "sl/exec/thread/detail/atomic.hpp"

#include <sl/meta/assert.hpp>
#include <sl/meta/func/lazy_eval.hpp>
#include <sl/meta/monad/maybe.hpp>
#include <sl/meta/tuple/for_each.hpp>
#include <sl/meta/type/pack.hpp>

#include <memory>

namespace sl::exec {
namespace detail {

template <
    typename ValueT,
    typename ErrorT,
    template <typename> typename Atomic,
    typename SlotCtorT,
    SomeSignal... SignalTs>
struct all_connection final {
    using slot_type = SlotFrom<SlotCtorT>;

private:
    template <std::size_t Index, typename ElementValueT>
    struct all_slot final {
        all_connection& self;

        void set_value(ElementValueT&& value) && noexcept { self.template set_value_impl<Index>(std::move(value)); }
        void set_error(ErrorT&& error) && noexcept { self.set_error_impl(Index, std::move(error)); }
        void set_null() && noexcept { self.set_null_impl(Index); }
    };

    template <std::size_t Index, typename ElementValueT>
    struct all_slot_ctor final {
        all_connection& self;

        constexpr all_slot<Index, ElementValueT> operator()() && noexcept {
            return all_slot<Index, ElementValueT>{ self };
        }
    };

    struct all_delete_this {
        all_connection* this_;
        void operator()() noexcept { delete this_; }
    };

    template <std::size_t Index, typename SignalT = meta::type::at_t<Index, SignalTs...>>
    using SlotCtor = all_slot_ctor<Index, typename SignalT::value_type>;
    template <std::size_t Index, typename SignalT = meta::type::at_t<Index, SignalTs...>>
    using Connection = ConnectionFor<SignalT, SlotCtor<Index, SignalT>>;
    static constexpr std::size_t N = sizeof...(SignalTs);

    template <std::size_t... Indexes>
    static auto derive_parallel_connection_type(std::index_sequence<Indexes...>)
        -> parallel_connection<all_delete_this, Atomic, Connection<Indexes>...>;
    using parallel_connection_type = decltype(derive_parallel_connection_type(std::make_index_sequence<N>()));

    template <std::size_t... Indexes>
    static auto
        make_connections(all_connection& self, std::tuple<SignalTs...>&& signals, std::index_sequence<Indexes...>) {
        return std::make_tuple(meta::lazy_eval{ [signal = std::move(std::get<Indexes>(signals)), &self]() mutable {
            return std::move(signal).subscribe(SlotCtor<Indexes>{ self });
        } }...);
    }

public:
    all_connection(std::tuple<SignalTs...>&& signals, serial_executor<Atomic>& an_executor, SlotCtorT slot_ctor)
        : parallel_{ make_connections(*this, std::move(signals), std::make_index_sequence<N>()),
                     an_executor,
                     all_delete_this{ this } },
          slot_{ std::move(slot_ctor)() } {}

public: // connection
    CancelHandle auto emit() && noexcept { return std::move(parallel_).emit(); }

private:
    template <std::size_t Index, typename ElementValueT>
    void set_value_impl(ElementValueT&& value) noexcept {
        std::get<Index>(maybe_results_).emplace(std::move(value));

        // synchronizing access to maybe_results_
        const bool is_last = parallel_.increment_and_check(1, std::memory_order::acq_rel);
        if (!is_last) {
            return;
        }

        if (!done_.exchange(true, std::memory_order::acq_rel)) {
            auto result = meta::for_each(
                [](auto&& maybe_result) {
                    DEBUG_ASSERT(maybe_result.has_value());
                    return std::move(maybe_result).value();
                },
                std::move(maybe_results_)
            );
            std::move(slot_).set_value(std::move(result));
        }

        parallel_.schedule_delete_this();
    }

    void set_error_impl(std::size_t index, ErrorT&& error) noexcept {
        if (!done_.exchange(true, std::memory_order::acq_rel)) {
            std::move(slot_).set_error(std::move(error));
            parallel_.schedule_try_cancel_beside(index);
        }

        const bool is_last = parallel_.increment_and_check();
        if (is_last) {
            parallel_.schedule_delete_this();
        }
    }

    void set_null_impl(std::size_t index) noexcept {
        if (!done_.exchange(true, std::memory_order::acq_rel)) {
            std::move(slot_).set_null();
            parallel_.schedule_try_cancel_beside(index);
        }

        const bool is_last = parallel_.increment_and_check();
        if (is_last) {
            parallel_.schedule_delete_this();
        }
    }

private:
    parallel_connection_type parallel_;
    std::tuple<meta::maybe<typename SignalTs::value_type>...> maybe_results_{};
    slot_type slot_;
    alignas(hardware_destructive_interference_size) Atomic<bool> done_{ false };
};

template <
    typename ValueT,
    typename ErrorT,
    template <typename> typename Atomic,
    typename SlotCtorT,
    SomeSignal... SignalTs>
struct all_connection_box final {
    using connection_type = all_connection<ValueT, ErrorT, Atomic, SlotCtorT, SignalTs...>;

public:
    all_connection_box(std::tuple<SignalTs...>&& signals, serial_executor<Atomic>& an_executor, SlotCtorT slot_ctor)
        : connection_{ std::make_unique<connection_type>(std::move(signals), an_executor, std::move(slot_ctor)) } {}

    CancelHandle auto emit() && noexcept {
        auto& a_connection = *DEBUG_ASSERT_VAL(connection_.release());
        return std::move(a_connection).emit();
    }

private:
    std::unique_ptr<connection_type> connection_;
};

template <template <typename> typename Atomic, SomeSignal... SignalTs>
    requires meta::type::are_same_v<typename SignalTs::error_type...>
struct [[nodiscard]] all_signal final {
    using value_type = std::tuple<typename SignalTs::value_type...>;
    using error_type = meta::type::head_t<typename SignalTs::error_type...>;

public:
    explicit all_signal(SignalTs&&... signals, serial_executor<Atomic>& an_executor)
        : signals_{ std::move(signals)... }, executor_{ an_executor } {}

    template <SlotCtor<value_type, error_type> SlotCtorT>
    constexpr Connection auto subscribe(SlotCtorT&& slot_ctor) && noexcept {
        return all_connection_box<value_type, error_type, Atomic, SlotCtorT, SignalTs...>{
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
constexpr auto all_(serial_executor<Atomic>& an_executor = detail::inline_serial_executor<Atomic>()) {
    return [&]<SomeSignal... SignalTs>(SignalTs... signals) {
        return detail::all_signal<Atomic, SignalTs...>{ std::move(signals)..., an_executor };
    };
}

template <SomeSignal... SignalTs>
constexpr SomeSignal auto all(SignalTs... signals) {
    return all_<detail::atomic>(detail::inline_serial_executor<detail::atomic>())(std::move(signals)...);
}

} // namespace sl::exec
