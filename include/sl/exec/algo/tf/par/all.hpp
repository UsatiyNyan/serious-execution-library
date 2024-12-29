//
// Created by usatiynyan.
// TODO: template from <Atomic> and allow "no-wait"
//

#pragma once

#include "sl/exec/algo/sched/inline.hpp"
#include "sl/exec/algo/tf/detail/transform_connection.hpp"
#include "sl/exec/model/concept.hpp"
#include "sl/exec/thread/detail/polyfill.hpp"

#include <sl/meta/lifetime/defer.hpp>
#include <sl/meta/lifetime/immovable.hpp>
#include <sl/meta/lifetime/lazy_eval.hpp>
#include <sl/meta/tuple/for_each.hpp>
#include <sl/meta/type/list.hpp>

#include <atomic>
#include <tl/optional.hpp>

namespace sl::exec {
namespace detail {

template <typename ValueT, typename ErrorT, Signal... SignalTs>
struct all_connection : meta::immovable {
private:
    template <
        std::size_t Idx,
        typename Signal = meta::type::at_t<Idx, SignalTs...>,
        typename ElementValueT = typename Signal::value_type>
    struct all_slot : slot<ElementValueT, ErrorT> {
        explicit all_slot(all_connection& self) : self_{ self } {}

        void set_value(ElementValueT&& value) & override {
            self_.template set_value_impl<Idx, ElementValueT>(std::move(value));
        }
        void set_error(ErrorT&& error) & override { self_.set_error_impl(std::move(error)); }
        void cancel() & override { self_.cancel_impl(); }

    private:
        all_connection& self_;
    };

    template <typename T>
    struct connections_derive;
    template <std::size_t... Idxs>
    struct connections_derive<std::index_sequence<Idxs...>> {
        using type = std::tuple<transform_connection<SignalTs, all_slot<Idxs>>...>;
    };
    using connections_type = typename connections_derive<std::index_sequence_for<SignalTs...>>::type;

public:
    all_connection(std::tuple<SignalTs...>&& signals, slot<ValueT, ErrorT>& slot)
        : connections_{ make_connections(std::move(signals), std::index_sequence_for<SignalTs...>{}) }, slot_{ slot } {}

    void emit() & {
        meta::for_each([](Connection auto& connection) { connection.emit(); }, connections_);
    };

private:
    template <std::size_t... Idxs>
    auto make_connections(std::tuple<SignalTs...>&& signals, std::index_sequence<Idxs...>) {
        return std::make_tuple(meta::lazy_eval{ [this, signal = std::move(signals)]() mutable {
            return transform_connection{
                /* .signal = */ std::get<Idxs>(std::move(signal)),
                /* .slot =  */ all_slot<Idxs>{ *this },
            };
        } }...);
    }

    [[nodiscard]] bool increment_and_check() {
        const std::uint32_t current_count = 1 + counter_.fetch_add(1, std::memory_order::relaxed);
        const bool is_last = current_count == sizeof...(SignalTs);
        return is_last;
    }

    template <std::size_t Idx, typename ElementValueT>
    void set_value_impl(ElementValueT&& value) {
        std::get<Idx>(maybe_results_).emplace(std::move(value));
        const bool is_last = increment_and_check();
        if (!is_last) {
            return;
        }

        meta::defer cleanup{ [this] { delete this; } };

        if (done_.exchange(true, std::memory_order::acq_rel)) {
            return;
        }

        auto result = meta::for_each(
            [](auto&& maybe_result) {
                DEBUG_ASSERT(maybe_result.has_value());
                return std::move(maybe_result).value();
            },
            std::move(maybe_results_)
        );
        slot_.set_value(std::move(result));
    }

    void set_error_impl(ErrorT&& error) {
        meta::defer cleanup{ [this] {
            const bool is_last = increment_and_check();
            if (is_last) {
                delete this;
            }
        } };

        if (!done_.exchange(true, std::memory_order::acq_rel)) {
            slot_.set_error(std::move(error));
        }
    }

    void cancel_impl() {
        meta::defer cleanup{ [this] {
            const bool is_last = increment_and_check();
            if (is_last) {
                delete this;
            }
        } };

        if (!done_.exchange(true, std::memory_order::acq_rel)) {
            slot_.cancel();
        }
    }

private:
    connections_type connections_;
    std::tuple<tl::optional<typename SignalTs::value_type>...> maybe_results_{};
    alignas(hardware_destructive_interference_size) std::atomic<std::uint32_t> counter_{ 0 };
    alignas(hardware_destructive_interference_size) std::atomic<bool> done_{ false };
    slot<ValueT, ErrorT>& slot_;
};

template <typename ValueT, typename ErrorT, Signal... SignalTs>
struct all_connection_box {
    all_connection_box(std::tuple<SignalTs...>&& signals, slot<ValueT, ErrorT>& slot)
        : connection_{ std::make_unique<all_connection<ValueT, ErrorT, SignalTs...>>(
              /* .signals = */ std::move(signals),
              /* .slot = */ slot
          ) } {}

    void emit() & { connection_.release()->emit(); }

private:
    std::unique_ptr<all_connection<ValueT, ErrorT, SignalTs...>> connection_;
};

template <Signal... SignalTs>
    requires meta::type::are_same_v<typename SignalTs::error_type...>
struct [[nodiscard]] all_signal {
    using value_type = std::tuple<typename SignalTs::value_type...>;
    using error_type = meta::type::head_t<typename SignalTs::error_type...>;

    template <typename... SignalTV>
    explicit all_signal(SignalTV&&... signals) : signals_{ std::forward<SignalTV>(signals)... } {}

    Connection auto subscribe(slot<value_type, error_type>& slot) && {
        return all_connection_box<value_type, error_type, SignalTs...>{
            /* .signals = */ std::move(signals_),
            /* .slot = */ slot,
        };
    }

    executor& get_executor() { return exec::inline_executor(); }

private:
    std::tuple<SignalTs...> signals_;
};

} // namespace detail

template <typename... SignalTV>
constexpr Signal auto all(SignalTV&&... signals) {
    return detail::all_signal<std::decay_t<SignalTV>...>{
        /* .signals = */ std::forward<SignalTV>(signals)...,
    };
}

} // namespace sl::exec
