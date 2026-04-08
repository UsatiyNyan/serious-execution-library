//
// Created by usatiynyan.
// NOTE: `any(...)` on signals breaks propagation of `try_cancel()`
//

#pragma once

#include "sl/exec/algo/emit/subscribe.hpp"
#include "sl/exec/algo/sync/detail/parallel.hpp"
#include "sl/exec/model/concept.hpp"
#include "sl/exec/thread/detail/atomic.hpp"

#include <sl/meta/assert.hpp>
#include <sl/meta/func/lazy_eval.hpp>
#include <sl/meta/type/pack.hpp>

#include <memory>

namespace sl::exec {
namespace detail {

template <typename ValueT, typename ErrorT, template <typename> typename Atomic, SomeSignal... SignalTs>
struct any_connection final {
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

    struct any_delete_this {
        any_connection* this_;
        void operator()() { delete this_; }
    };

    template <typename SignalT>
    using Connection = subscribe_connection<SignalT, any_slot>;
    static constexpr std::size_t N = sizeof...(SignalTs);

    template <std::size_t... Indexes>
    static auto
        make_connections(any_connection& self, std::tuple<SignalTs...>&& signals, std::index_sequence<Indexes...>) {
        return std::make_tuple(meta::lazy_eval{ [signal = std::move(std::get<Indexes>(signals)), &self]() mutable {
            return Connection<SignalTs>{ std::move(signal), [&] { return any_slot{ self, Indexes }; } };
        } }...);
    }

public:
    any_connection(std::tuple<SignalTs...>&& signals, executor& an_executor, slot<ValueT, ErrorT>& slot)
        : parallel_{ make_connections(*this, std::move(signals), std::make_index_sequence<N>()),
                     an_executor,
                     any_delete_this{ this } },
          slot_{ slot } {}

public: // connection
    cancel_handle& emit() && { return std::move(parallel_).emit(); };

private: // set_...
    void set_value_impl(std::size_t index, ValueT&& value) {
        if (!done_.exchange(true, std::memory_order::acq_rel)) {
            slot_.set_value(std::move(value));
            parallel_.schedule_try_cancel_beside(index);
        }

        const bool is_last = parallel_.increment_and_check();
        if (is_last) {
            parallel_.schedule_delete_this();
        }
    }

    void set_error_impl(ErrorT&& error) {
        const bool is_last = parallel_.increment_and_check();
        if (!is_last) {
            return;
        }

        if (!done_.exchange(true, std::memory_order::acq_rel)) {
            slot_.set_error(std::move(error));
        }

        parallel_.schedule_delete_this();
    }

    void set_null_impl() {
        const bool is_last = parallel_.increment_and_check();
        if (!is_last) {
            return;
        }

        if (!done_.exchange(true, std::memory_order::acq_rel)) {
            slot_.set_null();
        }

        parallel_.schedule_delete_this();
    }

private:
    parallel_connection<any_delete_this, Atomic, Connection<SignalTs>...> parallel_;
    slot<ValueT, ErrorT>& slot_;
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

template <template <typename> typename Atomic>
constexpr auto any_(executor& an_executor = inline_executor()) {
    return [&]<SomeSignal... SignalTs>(SignalTs... signals) {
        return detail::any_signal<Atomic, SignalTs...>{ std::move(signals)..., an_executor };
    };
}

template <SomeSignal... SignalTs>
constexpr SomeSignal auto any(SignalTs... signals) {
    return any_<detail::atomic>(inline_executor())(std::move(signals)...);
}

} // namespace sl::exec
