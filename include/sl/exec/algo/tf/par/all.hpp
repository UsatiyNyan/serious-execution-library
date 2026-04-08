//
// Created by usatiynyan.
// NOTE: `all(...)` on signals breaks propagation of `try_cancel()`
//

#pragma once

#include "sl/exec/algo/emit/subscribe.hpp"
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

template <typename ValueT, typename ErrorT, template <typename> typename Atomic, SomeSignal... SignalTs>
struct all_connection final {
private:
    template <std::size_t Index, typename ElementValueT>
    struct all_slot final : slot<ElementValueT, ErrorT> {
        explicit all_slot(all_connection& self) : self_{ self } {}

        void set_value(ElementValueT&& value) & override { self_.set_value_impl<Index>(std::move(value)); }
        void set_error(ErrorT&& error) & override { self_.set_error_impl(Index, std::move(error)); }
        void set_null() & override { self_.set_null_impl(Index); }

    private:
        all_connection& self_;
    };

    struct all_delete_this {
        all_connection* this_;
        void operator()() { delete this_; }
    };

    template <std::size_t Index, typename SignalT = meta::type::at_t<Index, SignalTs...>>
    using Slot = all_slot<Index, typename SignalT::value_type>;
    template <std::size_t Index, typename SignalT = meta::type::at_t<Index, SignalTs...>>
    using Connection = subscribe_connection<SignalT, Slot<Index, SignalT>>;
    static constexpr std::size_t N = sizeof...(SignalTs);

    template <std::size_t... Indexes>
    static auto derive_parallel_connection_type(std::index_sequence<Indexes...>)
        -> parallel_connection<all_delete_this, Atomic, Connection<Indexes>...>;
    using parallel_connection_type = decltype(derive_parallel_connection_type(std::make_index_sequence<N>()));

    template <std::size_t... Indexes>
    static auto
        make_connections(all_connection& self, std::tuple<SignalTs...>&& signals, std::index_sequence<Indexes...>) {
        return std::make_tuple(meta::lazy_eval{ [signal = std::move(std::get<Indexes>(signals)), &self]() mutable {
            return Connection<Indexes>{ std::move(signal), [&] { return Slot<Indexes>{ self }; } };
        } }...);
    }

public:
    all_connection(
        std::tuple<SignalTs...>&& signals,
        serial_executor<Atomic>& an_executor,
        slot<ValueT, ErrorT>& slot
    )
        : parallel_{ make_connections(*this, std::move(signals), std::make_index_sequence<N>()),
                     an_executor,
                     all_delete_this{ this } },
          slot_{ slot } {}

public: // connection
    cancel_handle& emit() && { return std::move(parallel_).emit(); };

private:
    template <std::size_t Index, typename ElementValueT>
    void set_value_impl(ElementValueT&& value) {
        std::get<Index>(maybe_results_).emplace(std::move(value));

        const bool is_last = parallel_.increment_and_check();
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
            slot_.set_value(std::move(result));
        }

        parallel_.schedule_delete_this();
    }

    void set_error_impl(std::size_t index, ErrorT&& error) {
        if (!done_.exchange(true, std::memory_order::acq_rel)) {
            slot_.set_error(std::move(error));
            parallel_.schedule_try_cancel_beside(index);
        }

        const bool is_last = parallel_.increment_and_check();
        if (is_last) {
            parallel_.schedule_delete_this();
        }
    }

    void set_null_impl(std::size_t index) {
        if (!done_.exchange(true, std::memory_order::acq_rel)) {
            slot_.set_null();
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
    slot<ValueT, ErrorT>& slot_;
    alignas(hardware_destructive_interference_size) Atomic<bool> done_{ false };
};

template <typename ValueT, typename ErrorT, template <typename> typename Atomic, SomeSignal... SignalTs>
struct all_connection_box final : connection {
    using connection_type = all_connection<ValueT, ErrorT, Atomic, SignalTs...>;

public:
    all_connection_box(
        std::tuple<SignalTs...>&& signals,
        serial_executor<Atomic>& an_executor,
        slot<ValueT, ErrorT>& slot
    )
        : connection_{ std::make_unique<connection_type>(std::move(signals), an_executor, slot) } {}

    cancel_handle& emit() && override {
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

    all_connection_box<value_type, error_type, Atomic, SignalTs...> subscribe(slot<value_type, error_type>& slot) && {
        return all_connection_box<value_type, error_type, Atomic, SignalTs...>{
            std::move(signals_),
            executor_,
            slot,
        };
    }

    executor& get_executor() { return executor_.get_inner(); }

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
