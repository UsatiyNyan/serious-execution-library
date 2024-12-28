//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/algo/sched/inline.hpp"
#include "sl/exec/algo/seq/transform_connection.hpp"
#include "sl/exec/model/concept.hpp"
#include "sl/exec/thread/detail/polyfill.hpp"

#include <sl/meta/lifetime/defer.hpp>
#include <sl/meta/lifetime/immovable.hpp>
#include <sl/meta/tuple/for_each.hpp>
#include <sl/meta/type/list.hpp>

#include <atomic>

namespace sl::exec {
namespace detail {

template <typename ValueT, typename ErrorT, Signal... SignalTs>
struct any_connection : meta::immovable {
private:
    struct any_slot : slot<ValueT, ErrorT> {
        explicit any_slot(any_connection& self) : self_{ self } {}

        void set_value(ValueT&& value) & override { self_.set_value_impl(std::move(value)); }
        void set_error(ErrorT&& error) & override { self_.set_error_impl(std::move(error)); }
        void cancel() & override { self_.cancel_impl(); }

    private:
        any_connection& self_;
    };

public:
    any_connection(std::tuple<SignalTs...>&& signals, slot<ValueT, ErrorT>& slot)
        : connections_{ meta::for_each(
              [this](auto&& signal) {
                  return transform_connection{
                      /* .signal = */ std::move(signal),
                      /* .slot =  */ any_slot{ *this },
                  };
              },
              std::move(signals)
          ) },
          slot_{ slot } {}

    void emit() & {
        meta::for_each([](Connection auto& connection) { connection.emit(); }, connections_);
    };

private:
    [[nodiscard]] bool increment_and_check() {
        const std::uint32_t current_count = 1 + counter_.fetch_add(1, std::memory_order::relaxed);
        const bool is_last = current_count == sizeof...(SignalTs);
        return is_last;
    }

    void set_value_impl(ValueT&& value) {
        meta::defer cleanup{ [this] {
            const bool is_last = increment_and_check();
            if (is_last) {
                delete this;
            }
        } };

        if (!done_.exchange(true, std::memory_order::acq_rel)) {
            slot_.set_value(std::move(value));
        }
    }

    void set_error_impl(ErrorT&& error) {
        const bool is_last = increment_and_check();
        if (!is_last) {
            return;
        }

        meta::defer cleanup{ [this] { delete this; } };

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
    std::tuple<transform_connection<SignalTs, slot<ValueT, ErrorT>>...> connections_;
    alignas(hardware_destructive_interference_size) std::atomic<std::uint32_t> counter_{ 0 };
    alignas(hardware_destructive_interference_size) std::atomic<bool> done_{ false };
    slot<ValueT, ErrorT>& slot_;
};

template <typename ValueT, typename ErrorT, Signal... SignalTs>
struct any_connection_box {
    any_connection_box(std::tuple<SignalTs...>&& signals, slot<ValueT, ErrorT>& slot)
        : connection_{ std::make_unique<any_connection>(
              /* .signals = */ std::move(signals),
              /* .slot = */ slot
          ) } {}

    void emit() & { connection_.release()->emit(); }

private:
    std::unique_ptr<any_connection<ValueT, ErrorT, SignalTs...>> connection_;
};

template <Signal... SignalTs>
    requires meta::type::are_same_v<typename SignalTs::value_type...>
             && meta::type::are_same_v<typename SignalTs::error_type...>
struct [[nodiscard]] any_signal {
    using value_type = meta::type::head_t<typename SignalTs::value_type...>;
    using error_type = meta::type::head_t<typename SignalTs::error_type...>;

    template <typename... SignalTV>
    explicit any_signal(SignalTV&&... signals) : signals_{ std::forward<SignalTV>(signals)... } {}

    Connection auto subscribe(slot<value_type, error_type>& slot) && {
        return any_connection_box<value_type, error_type, SignalTs...>{
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
constexpr Signal auto any(SignalTV&&... signals) {
    return detail::any_signal<std::decay_t<SignalTV>...>{
        /* .signals = */ std::forward<SignalTV>(signals)...,
    };
}

} // namespace sl::exec
