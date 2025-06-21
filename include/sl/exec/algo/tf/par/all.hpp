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
#include <sl/meta/monad/maybe.hpp>
#include <sl/meta/traits/unique.hpp>
#include <sl/meta/tuple/for_each.hpp>
#include <sl/meta/type/pack.hpp>

namespace sl::exec {
namespace detail {

template <typename ValueT, typename ErrorT, template <typename> typename Atomic, SomeSignal... SignalTs>
struct all_connection : cancel_mixin {
private:
    template <std::size_t Index, typename ElementValueT>
    struct all_slot : slot<ElementValueT, ErrorT> {
        explicit all_slot(all_connection& self) : self_{ self } {}

        void set_value(ElementValueT&& value) & override { self_.set_value_impl<Index>(std::move(value)); }
        void set_error(ErrorT&& error) & override { self_.set_error_impl(Index, std::move(error)); }
        void set_null() & override { self_.set_null_impl(Index); }

    private:
        all_connection& self_;
    };

    static constexpr std::size_t signals_count = sizeof...(SignalTs);

    template <std::size_t Index, typename SignalT = meta::type::at_t<Index, SignalTs...>>
    using slot_type = all_slot<Index, typename SignalT::value_type>;

    template <std::size_t Index, typename SignalT = meta::type::at_t<Index, SignalTs...>>
    using connection_type = subscribe_connection<SignalT, slot_type<Index, SignalT>>;

    template <std::size_t... Indexes>
    static auto derive_connections_type(std::index_sequence<Indexes...>) -> std::tuple<connection_type<Indexes>...>;
    using connections_type = decltype(derive_connections_type(std::make_index_sequence<signals_count>()));

    template <std::size_t... Indexes>
    static connections_type
        make_connections(all_connection& self, std::tuple<SignalTs...>&& signals, std::index_sequence<Indexes...>) {
        return std::make_tuple(meta::lazy_eval{ [signal = std::move(std::get<Indexes>(signals)), &self]() mutable {
            return connection_type<Indexes>{ std::move(signal), [&] { return slot_type<Indexes>{ self }; } };
        } }...);
    }

    static std::array<cancel_mixin*, signals_count> make_cancel_handles(connections_type& connections) {
        std::array<cancel_mixin*, signals_count> cancel_handles;
        std::size_t i = 0;
        meta::for_each([&](Connection auto& x) { cancel_handles[i++] = &x.get_cancel_handle(); }, connections);
        ASSERT(i == signals_count);
        return cancel_handles;
    }

public:
    all_connection(std::tuple<SignalTs...>&& signals, slot<ValueT, ErrorT>& slot)
        : connections_{ make_connections(*this, std::move(signals), std::make_index_sequence<signals_count>()) },
          cancel_handles_{ make_cancel_handles(connections_) }, slot_{ slot } {
        slot_.intrusive_next = this;
    }

    // Connection
    cancel_mixin& get_cancel_handle() & {
        ASSERT(slot_.intrusive_next == this);
        return slot_;
    }
    void emit() && {
        meta::for_each([](Connection auto&& connection) { std::move(connection).emit(); }, std::move(connections_));
    };

    // cancel_mixin
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

    template <std::size_t Index, typename ElementValueT>
    void set_value_impl(ElementValueT&& value) {
        std::get<Index>(maybe_results_).emplace(std::move(value));

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

    void set_error_impl(std::size_t index, ErrorT&& error) {
        meta::defer cleanup{ [this] {
            const bool is_last = increment_and_check();
            if (is_last) {
                delete this;
            }
        } };

        if (!done_.exchange(true, std::memory_order::acq_rel)) {
            slot_.set_error(std::move(error));
            try_cancel_beside(index);
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
    connections_type connections_;
    std::array<cancel_mixin*, signals_count> cancel_handles_;
    std::tuple<meta::maybe<typename SignalTs::value_type>...> maybe_results_{};
    slot<ValueT, ErrorT>& slot_;
    alignas(hardware_destructive_interference_size) Atomic<std::uint32_t> counter_{ 0 };
    alignas(hardware_destructive_interference_size) Atomic<bool> done_{ false };
};

template <typename ValueT, typename ErrorT, template <typename> typename Atomic, SomeSignal... SignalTs>
struct all_connection_box {
    using connection_type = all_connection<ValueT, ErrorT, Atomic, SignalTs...>;

public:
    all_connection_box(std::tuple<SignalTs...>&& signals, slot<ValueT, ErrorT>& slot)
        : connection_{ std::make_unique<connection_type>(std::move(signals), slot) } {}

    cancel_mixin& get_cancel_handle() & {
        ASSERT(connection_);
        return connection_->get_cancel_handle();
    }

    void emit() && {
        auto& connection = *DEBUG_ASSERT_VAL(connection_.release());
        std::move(connection).emit();
    }

private:
    std::unique_ptr<connection_type> connection_;
};

template <template <typename> typename Atomic, SomeSignal... SignalTs>
    requires meta::type::are_same_v<typename SignalTs::error_type...>
struct [[nodiscard]] all_signal {
    using value_type = std::tuple<typename SignalTs::value_type...>;
    using error_type = meta::type::head_t<typename SignalTs::error_type...>;

public:
    explicit all_signal(SignalTs&&... signals) : signals_{ std::move(signals)... } {}

    Connection auto subscribe(slot<value_type, error_type>& slot) && {
        return all_connection_box<value_type, error_type, Atomic, SignalTs...>{ std::move(signals_), slot };
    }

    executor& get_executor() { return exec::inline_executor(); }

private:
    std::tuple<SignalTs...> signals_;
};

} // namespace detail

template <template <typename> typename Atomic, typename... SignalTs>
constexpr SomeSignal auto all_(SignalTs... signals) {
    return detail::all_signal<Atomic, SignalTs...>{ std::move(signals)... };
}

template <typename... SignalTs>
constexpr SomeSignal auto all(SignalTs... signals) {
    return all_<detail::atomic>(std::move(signals)...);
}

} // namespace sl::exec
