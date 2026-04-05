//
// Created by usatiynyan.
// NOTE: `all(...)` on signals breaks propagation of `try_cancel()`
//

#pragma once

#include "sl/exec/algo/emit/subscribe.hpp"
#include "sl/exec/algo/sync/serial.hpp"
#include "sl/exec/model/concept.hpp"
#include "sl/exec/model/connection.hpp"
#include "sl/exec/model/executor.hpp"
#include "sl/exec/thread/detail/atomic.hpp"
#include "sl/exec/thread/detail/polyfill.hpp"

#include <sl/meta/assert.hpp>
#include <sl/meta/func/lazy_eval.hpp>
#include <sl/meta/monad/maybe.hpp>
#include <sl/meta/traits/unique.hpp>
#include <sl/meta/tuple/for_each.hpp>
#include <sl/meta/type/pack.hpp>

#include <memory>

namespace sl::exec {
namespace detail {

template <typename ValueT, typename ErrorT, template <typename> typename Atomic, SomeSignal... SignalTs>
struct any_connection final : connection {
private:
    struct any_slot final : slot<ValueT, ErrorT> {
        any_slot(any_connection& self, std::size_t index) : self_{ self }, index_{ index } {}

        void set_value(ValueT&& value) & override { self_.set_value_impl(index_, std::move(value)); }
        void set_error(ErrorT&& error) & override { self_.set_error_impl(std::move(error)); }
        void set_null() & override { self_.set_null_impl(); }

    private:
        any_connection& self_;
        std::size_t index_;
    };

    static constexpr std::size_t signals_count = sizeof...(SignalTs);

    template <typename SignalT>
    using connection_type = subscribe_connection<SignalT, any_slot>;
    using connections_type = std::tuple<connection_type<SignalTs>...>;

    template <std::size_t... Indexes>
    static connections_type
        make_connections(any_connection& self, std::tuple<SignalTs...>&& signals, std::index_sequence<Indexes...>) {
        return std::make_tuple(meta::lazy_eval{ [signal = std::move(std::get<Indexes>(signals)), &self]() mutable {
            return connection_type<SignalTs>{ std::move(signal), [&] { return any_slot{ self, Indexes }; } };
        } }...);
    }

    struct emit_task : task_node {
        emit_task(any_connection& self, std::array<cancel_handle&, signals_count> cancel_handles)
            : cancel_handles_{ cancel_handles }, self_{ self } {}

        void execute() noexcept override { self_.serialized_emit(cancel_handles_); }
        void cancel() noexcept override {}

    private:
        std::array<cancel_handle&, signals_count> cancel_handles_;
        any_connection& self_;
    };

    struct try_cancel_beside_task : task_node {
        try_cancel_beside_task(any_connection& self, std::size_t excluded_index)
            : self_{ self }, excluded_index_{ excluded_index } {}

        void execute() noexcept override { self_.serialized_try_cancel_beside(excluded_index_); }
        void cancel() noexcept override {}

    private:
        any_connection& self_;
        std::size_t excluded_index_;
    };

    struct delete_this_task : task_node {
        explicit delete_this_task(any_connection& self) : self_{ self } {}

        void execute() noexcept override { self_.serialized_delete_this(); }
        void cancel() noexcept override {}

    private:
        any_connection& self_;
    };

public:
    any_connection(std::tuple<SignalTs...>&& signals, executor& an_executor, slot<ValueT, ErrorT>& slot)
        : connections_{ make_connections(*this, std::move(signals), std::make_index_sequence<signals_count>()) },
          serial_executor_{ an_executor }, slot_{ slot } {}

public: // connection
    cancel_handle& emit() && override {
        std::array<cancel_handle&, signals_count> cancel_handles = std::apply(
            [](auto&&... a_connection) {
                return std::array<cancel_handle&, signals_count>{ std::move(a_connection).emit()... };
            },
            std::move(connections_)
        );

        DEBUG_ASSERT(!serial_tasks_.emit.has_value());
        serial_executor_.schedule(serial_tasks_.emit.emplace(*this, cancel_handles));

        return dummy_cancel_handle();
    };

private:
    void try_cancel_beside(std::size_t excluded_index) {
        DEBUG_ASSERT(!serial_tasks_.try_cancel_beside.has_value());
        serial_executor_.schedule(serial_tasks_.try_cancel_beside.emplace(*this, excluded_index));
    }

    void schedule_delete_this() {
        DEBUG_ASSERT(!serial_tasks_.delete_this.has_value());
        serial_executor_.schedule(serial_tasks_.delete_this.emplace(*this));
    }

private: // set_...
    void set_value_impl(std::size_t index, ValueT&& value) {
        if (!done_.exchange(true, std::memory_order::acq_rel)) {
            slot_.set_value(std::move(value));
            try_cancel_beside(index);
        }

        const bool is_last = increment_and_check();
        if (is_last) {
            schedule_delete_this();
        }
    }

    void set_error_impl(ErrorT&& error) {
        const bool is_last = increment_and_check();
        if (!is_last) {
            return;
        }

        if (!done_.exchange(true, std::memory_order::acq_rel)) {
            slot_.set_error(std::move(error));
        }

        schedule_delete_this();
    }

    void set_null_impl() {
        const bool is_last = increment_and_check();
        if (!is_last) {
            return;
        }

        if (!done_.exchange(true, std::memory_order::acq_rel)) {
            slot_.set_null();
        }

        schedule_delete_this();
    }

private: // serialized
    void serialized_emit(const std::array<cancel_handle&, signals_count>& cancel_handles) {
        serial_state_.cancel_handles.emplace(cancel_handles);

        // can't touch tasks_.try_cancel or tasks_.try_cancel_beside
        if (serial_state_.cancel_request.has_value()) {
            try_cancel_beside_impl(cancel_handles, serial_state_.cancel_request.value());
            serial_state_.cancel_request.reset();
        }

        if (serial_state_.delete_requested) {
            delete this;
        }
    }

    void serialized_try_cancel_beside(std::size_t excluded_index) {
        // means try_cancel_beside already has signalled
        // or cancelling during emit
        if (serial_state_.cancel_request.has_value() || !serial_state_.cancel_handles.has_value()) {
            serial_state_.cancel_request.emplace(excluded_index);
            return;
        }
        try_cancel_beside_impl(serial_state_.cancel_handles.value(), excluded_index);
    }

    void serialized_delete_this() {
        // deleting during emit
        if (!serial_state_.cancel_handles.has_value()) {
            serial_state_.delete_requested = true;
            return;
        }

        delete this;
    }

private: // refcount
    [[nodiscard]] bool increment_and_check(std::uint32_t diff = 1) {
        const std::uint32_t current_count = diff + counter_.fetch_add(diff, std::memory_order::relaxed);
        const bool is_last = current_count == signals_count;
        return is_last;
    }

    static void try_cancel_beside_impl(
        const std::array<cancel_handle&, signals_count>& cancel_handles,
        std::size_t excluded_index
    ) {
        for (std::size_t i = 0; i != cancel_handles.size(); ++i) {
            if (i == excluded_index) {
                continue;
            }
            cancel_handles[i].try_cancel();
        }
    }

private:
    connections_type connections_;

    serial_executor<Atomic> serial_executor_;
    struct serial_tasks {
        meta::maybe<emit_task> emit{};
        meta::maybe<try_cancel_beside_task> try_cancel_beside{};
        meta::maybe<delete_this_task> delete_this{};
    } serial_tasks_;

    struct serial_state {
        meta::maybe<std::array<cancel_handle&, signals_count>> cancel_handles{};
        meta::maybe<std::size_t> cancel_request = {};
        bool delete_requested = false;
    } serial_state_;

    slot<ValueT, ErrorT>& slot_;
    alignas(hardware_destructive_interference_size) Atomic<std::uint32_t> counter_{ 0 };
    alignas(hardware_destructive_interference_size) Atomic<bool> done_{ false };
};

template <typename ValueT, typename ErrorT, template <typename> typename Atomic, SomeSignal... SignalTs>
struct any_connection_box final : connection {
    using connection_type = any_connection<ValueT, ErrorT, Atomic, SignalTs...>;

public:
    any_connection_box(std::tuple<SignalTs...>&& signals, executor& an_executor, slot<ValueT, ErrorT>& slot)
        : connection_{ std::make_unique<connection_type>(std::move(signals), an_executor, slot) } {}

    cancel_handle& emit() && override {
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
    explicit any_signal(SignalTs&&... signals, executor& an_executor)
        : signals_{ std::move(signals)... }, executor_{ an_executor } {}

    any_connection_box<value_type, error_type, Atomic, SignalTs...> subscribe(slot<value_type, error_type>& slot) && {
        return any_connection_box<value_type, error_type, Atomic, SignalTs...>{
            std::move(signals_),
            executor_,
            slot,
        };
    }

    executor& get_executor() { return executor_; }

private:
    std::tuple<SignalTs...> signals_;
    executor& executor_;
};

} // namespace detail

template <template <typename> typename Atomic, SomeSignal... SignalTs>
constexpr SomeSignal auto any_(SignalTs... signals, executor& an_executor = inline_executor()) {
    return detail::any_signal<Atomic, SignalTs...>{ std::move(signals)..., an_executor };
}

template <SomeSignal... SignalTs>
constexpr SomeSignal auto any(SignalTs... signals, executor& an_executor = inline_executor()) {
    return any_<detail::atomic>(std::move(signals)..., an_executor);
}

} // namespace sl::exec
