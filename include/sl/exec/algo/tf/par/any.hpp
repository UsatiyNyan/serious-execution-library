//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/algo/emit/subscribe.hpp"
#include "sl/exec/algo/sched/inline.hpp"
#include "sl/exec/model/concept.hpp"
#include "sl/exec/thread/detail/atomic.hpp"
#include "sl/exec/thread/detail/polyfill.hpp"

#include <sl/meta/func/lazy_eval.hpp>
#include <sl/meta/lifetime/defer.hpp>
#include <sl/meta/traits/unique.hpp>
#include <sl/meta/tuple/enumerate.hpp>
#include <sl/meta/tuple/for_each.hpp>
#include <sl/meta/type/pack.hpp>

namespace sl::exec {
namespace detail {

template <typename ValueT, typename ErrorT, template <typename> typename Atomic, SomeSignal... SignalTs>
struct any_connection
    : cancel_mixin
    , meta::immovable {
private:
    struct any_slot : slot<ValueT, ErrorT> {
        any_slot(any_connection& self, std::size_t index) : self_{ self }, index_{ index } {}

        void set_value(ValueT&& value) & override { self_.set_value_impl(index_, std::move(value)); }
        void set_error(ErrorT&& error) & override { self_.set_error_impl(std::move(error)); }
        void set_null() & override { self_.set_null_impl(index_); }

    private:
        any_connection& self_;
        std::size_t index_;
    };

    static constexpr std::size_t signals_count = sizeof...(SignalTs);

    static std::tuple<subscribe_connection<SignalTs, any_slot>...>
        make_connections(any_connection& self, std::tuple<SignalTs...> signals) {
        return meta::for_each(
            [&self](auto index_signal) {
                return meta::lazy_eval{ [&self, index_signal = std::move(index_signal)]() mutable {
                    auto& [index, signal] = index_signal;
                    return subscribe_connection{ std::move(signal), any_slot{ self, index } };
                } };
            },
            meta::enumerate(std::move(signals))
        );
    }

    static std::array<cancel_mixin*, signals_count>
        make_cancel_handles(std::tuple<subscribe_connection<SignalTs, any_slot>...>& connections) {
        std::array<cancel_mixin*, signals_count> cancel_handles;
        std::size_t i = 0;
        meta::for_each([&](Connection auto& x) { cancel_handles[i++] = &x.get_cancel_handle(); }, connections);
        ASSERT(i == signals_count);
        return cancel_handles;
    }

public:
    any_connection(std::tuple<SignalTs...> signals, slot<ValueT, ErrorT>& slot)
        : connections_{ make_connections(*this, std::move(signals)) },
          cancel_handles_{ make_cancel_handles(connections_) }, slot_{ slot } {}

    // Connection
    cancel_mixin& get_cancel_handle() & {
        ASSERT(slot_.intrusive_next == this);
        return slot_;
    }
    void emit() && {
        meta::for_each([](Connection auto&& connection) { std::move(connection).emit(); }, std::move(connections_));
    };

    // cancel_mixin
    void setup_cancellation() & override { slot_.intrusive_next = this; }
    bool try_cancel() & override {
        if (done_.exchange(true, std::memory_order::acq_rel)) {
            return false;
        }

        std::uint32_t cancel_counter = 0;

        for (std::size_t i = 0; i != cancel_handles_.size(); ++i) {
            cancel_mixin& cancel_handle = *cancel_handles_[i];
            const bool is_cancelled = cancel_handle.try_cancel();
            cancel_counter += static_cast<std::uint32_t>(is_cancelled);
        }

        const bool is_last = increment_and_check(cancel_counter);
        if (is_last) {
            delete this;
        }

        return true;
    }

private:
    [[nodiscard]] bool increment_and_check(std::uint32_t diff = 1) {
        const std::uint32_t current_count = diff + counter_.fetch_add(diff, std::memory_order::relaxed);
        const bool is_last = current_count == signals_count;
        return is_last;
    }

    void set_value_impl(std::size_t index, ValueT&& value) {
        meta::defer cleanup{ [this] {
            const bool is_last = increment_and_check();
            if (is_last) {
                delete this;
            }
        } };

        if (!done_.exchange(true, std::memory_order::acq_rel)) {
            slot_.set_value(std::move(value));
            try_cancel_beside(index);
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

    void set_null_impl(std::size_t index) {
        meta::defer cleanup{ [this] {
            const bool is_last = increment_and_check();
            if (is_last) {
                delete this;
            }
        } };

        if (!done_.exchange(true, std::memory_order::acq_rel)) {
            slot_.set_null();
            try_cancel_beside(index);
        }
    }

    void try_cancel_beside(std::size_t excluded_index) {
        std::uint32_t cancel_counter = 0;

        for (std::size_t i = 0; i != cancel_handles_.size(); ++i) {
            if (i == excluded_index) {
                continue;
            }
            cancel_mixin& cancel_handle = *cancel_handles_[i];
            const bool is_cancelled = cancel_handle.try_cancel();
            cancel_counter += static_cast<std::uint32_t>(is_cancelled);
        }

        const bool should_not_be_last = increment_and_check(cancel_counter);
        ASSERT(!should_not_be_last);
    }

private:
    std::tuple<subscribe_connection<SignalTs, any_slot>...> connections_;
    std::array<cancel_mixin*, signals_count> cancel_handles_;
    slot<ValueT, ErrorT>& slot_;
    alignas(hardware_destructive_interference_size) Atomic<std::uint32_t> counter_{ 0 };
    alignas(hardware_destructive_interference_size) Atomic<bool> done_{ false };
};

template <typename ValueT, typename ErrorT, template <typename> typename Atomic, SomeSignal... SignalTs>
struct any_connection_box {
    any_connection_box(std::tuple<SignalTs...> signals, slot<ValueT, ErrorT>& slot)
        : connection_{ std::make_unique<any_connection<ValueT, ErrorT, Atomic, SignalTs...>>(
              /* .signals = */ std::move(signals),
              /* .slot = */ slot
          ) } {
        connection_->setup_cancellation();
    }

    cancel_mixin& get_cancel_handle() & {
        ASSERT(connection_);
        return connection_->get_cancel_handle();
    }

    void emit() && {
        auto& connection = *DEBUG_ASSERT_VAL(connection_.release());
        std::move(connection).emit();
    }

private:
    std::unique_ptr<any_connection<ValueT, ErrorT, Atomic, SignalTs...>> connection_;
};

template <template <typename> typename Atomic, SomeSignal... SignalTs>
    requires meta::type::are_same_v<typename SignalTs::value_type...>
             && meta::type::are_same_v<typename SignalTs::error_type...>
struct [[nodiscard]] any_signal {
    using value_type = meta::type::head_t<typename SignalTs::value_type...>;
    using error_type = meta::type::head_t<typename SignalTs::error_type...>;

    explicit any_signal(SignalTs... signals) : signals_{ std::move(signals)... } {}

    Connection auto subscribe(slot<value_type, error_type>& slot) && {
        return any_connection_box<value_type, error_type, Atomic, SignalTs...>{
            /* .signals = */ std::move(signals_),
            /* .slot = */ slot,
        };
    }

    executor& get_executor() { return exec::inline_executor(); }

private:
    std::tuple<SignalTs...> signals_;
};

} // namespace detail

template <template <typename> typename Atomic, SomeSignal... SignalTs>
constexpr SomeSignal auto any_(SignalTs... signals) {
    return detail::any_signal<Atomic, SignalTs...>{ std::forward<SignalTs>(signals)... };
}

template <SomeSignal... SignalTs>
constexpr SomeSignal auto any(SignalTs... signals) {
    return any_<detail::atomic>(std::move(signals)...);
}

} // namespace sl::exec
