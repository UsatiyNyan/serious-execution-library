//
// Created by usatiynyan.
// NOTE: parallel_connection breaks propagation of `try_cancel()`
//

#pragma once

#include "sl/exec/algo/sync/serial.hpp"
#include "sl/exec/model/concept.hpp"
#include "sl/exec/model/executor.hpp"
#include "sl/exec/thread/detail/polyfill.hpp"

#include <sl/meta/match/overloaded.hpp>
#include <sl/meta/monad/maybe.hpp>
#include <sl/meta/tuple/for_each.hpp>

#include <cstdint>

namespace sl::exec::detail {

template <typename DeleteThisT, template <typename> typename Atomic, typename... ConnectionTs>
struct parallel_connection {
private:
    static constexpr std::size_t N = sizeof...(ConnectionTs);
    using cancel_handles_type = std::tuple<decltype(std::declval<ConnectionTs&&>().emit())...>;

    struct emit_task : task_node {
        emit_task(parallel_connection& self, cancel_handles_type cancel_handles)
            : cancel_handles_{ std::move(cancel_handles) }, self_{ self } {}

        void execute() noexcept override { self_.serialized_emit(std::move(cancel_handles_)); }
        void cancel() noexcept override {
            // FIXME: leaking for now, should be alright
        }

    private:
        cancel_handles_type cancel_handles_;
        parallel_connection& self_;
    };

    struct try_cancel_beside_task : task_node {
        try_cancel_beside_task(parallel_connection& self, std::size_t excluded_index)
            : self_{ self }, excluded_index_{ excluded_index } {}

        void execute() noexcept override { self_.serialized_try_cancel_beside(excluded_index_); }
        void cancel() noexcept override {
            // FIXME: leaking for now, should be alright
        }

    private:
        parallel_connection& self_;
        std::size_t excluded_index_;
    };

    struct delete_this_task : task_node {
        explicit delete_this_task(parallel_connection& self) : self_{ self } {}

        void execute() noexcept override { self_.serialized_delete_this(); }
        void cancel() noexcept override { execute(); }

    private:
        parallel_connection& self_;
    };

public:
    template <typename... LazyTs>
    parallel_connection(
        std::tuple<LazyTs...>&& lazy_connections,
        serial_executor<Atomic>& an_executor,
        DeleteThisT delete_this
    )
        : connections_{ std::move(lazy_connections) }, executor_{ an_executor },
          delete_this_{ std::move(delete_this) } {}

public: // connection
    CancelHandle auto emit() && noexcept {
        auto cancel_handles = std::apply(
            [](auto&&... connections) { return cancel_handles_type{ std::move(connections).emit()... }; },
            std::move(connections_)
        );
        emit_impl(std::move(cancel_handles));
        return dummy_cancel_handle{};
    }

    // all connections are subscribe_connection
    // Emit in sorted order by ordering, but store cancel_handles in ORIGINAL order
    // This is critical: schedule_try_cancel_beside uses original indices
    CancelHandle auto emit_ordered() && noexcept {
        auto cancel_handles = emit_in_order(std::make_index_sequence<N>{});
        emit_impl(std::move(cancel_handles));
        return dummy_cancel_handle{};
    }

private:
    template <std::size_t... Is>
    cancel_handles_type emit_in_order(std::index_sequence<Is...>) {
        std::array<std::pair<std::uintptr_t, std::size_t>, N> orderings{
            std::pair{ get_connection_ordering(std::get<Is>(connections_)), Is }... //
        };
        std::stable_sort(orderings.begin(), orderings.end(), [](const auto& x, const auto& y) {
            return x.first < y.first;
        });

        using maybe_handles_type = std::tuple<meta::maybe<std::tuple_element_t<Is, cancel_handles_type>>...>;
        maybe_handles_type maybe_result;
        for (const auto& [_, idx] : orderings) {
            ((idx == Is ? (std::get<Is>(maybe_result).emplace(std::move(std::get<Is>(connections_)).emit()), 0) : 0),
             ...);
        }
        return cancel_handles_type{ std::move(*std::get<Is>(maybe_result))... };
    }

    void emit_impl(cancel_handles_type cancel_handles) {
        DEBUG_ASSERT(!tasks_.emit.has_value());
        executor_.schedule(tasks_.emit.emplace(*this, std::move(cancel_handles)));
    }

public: // parallel
    [[nodiscard]] bool increment_and_check(std::uint32_t diff = 1, std::memory_order mo = std::memory_order::relaxed) {
        const std::uint32_t current_count = diff + counter_.fetch_add(diff, mo);
        const bool is_last = current_count == N;
        return is_last;
    }

    void schedule_try_cancel_beside(std::size_t excluded_index) {
        DEBUG_ASSERT(!tasks_.try_cancel_beside.has_value());
        executor_.schedule(tasks_.try_cancel_beside.emplace(*this, excluded_index));
    }

    void schedule_delete_this() {
        DEBUG_ASSERT(!tasks_.delete_this.has_value());
        executor_.schedule(tasks_.delete_this.emplace(*this));
    }

private: // serialized
    template <std::size_t... Is>
    static void try_cancel_beside_impl(
        cancel_handles_type& cancel_handles,
        std::size_t excluded_index,
        std::index_sequence<Is...>
    ) {
        ((Is != excluded_index ? (std::move(std::get<Is>(cancel_handles)).try_cancel(), 0) : 0), ...);
    }

    static void serialized_try_cancel_beside_impl(cancel_handles_type& cancel_handles, std::size_t excluded_index) {
        try_cancel_beside_impl(cancel_handles, excluded_index, std::make_index_sequence<N>{});
    }

    void serialized_emit(cancel_handles_type cancel_handles) {
        state_.cancel_handles.emplace(std::move(cancel_handles));

        // can't touch tasks_.try_cancel or tasks_.try_cancel_beside
        if (state_.cancel_request.has_value()) {
            serialized_try_cancel_beside_impl(state_.cancel_handles.value(), state_.cancel_request.value());
            state_.cancel_request.reset();
        }

        if (state_.delete_requested) {
            delete_this_();
        }
    }

    void serialized_try_cancel_beside(std::size_t excluded_index) {
        // try_cancel_beside already has signalled
        if (state_.cancel_request.has_value()) {
            return;
        }

        // cancelling during emit
        if (!state_.cancel_handles.has_value()) {
            state_.cancel_request.emplace(excluded_index);
            return;
        }

        serialized_try_cancel_beside_impl(state_.cancel_handles.value(), excluded_index);
    }

    void serialized_delete_this() {
        // deleting during emit
        if (!state_.cancel_handles.has_value()) {
            state_.delete_requested = true;
            return;
        }

        delete_this_();
    }

private:
    std::tuple<ConnectionTs...> connections_;

    serial_executor<Atomic>& executor_;
    struct tasks {
        meta::maybe<emit_task> emit{};
        meta::maybe<try_cancel_beside_task> try_cancel_beside{};
        meta::maybe<delete_this_task> delete_this{};
    } tasks_;

    struct state {
        meta::maybe<cancel_handles_type> cancel_handles{};
        meta::maybe<std::size_t> cancel_request{};
        bool delete_requested = false;
    } state_;

    DeleteThisT delete_this_;

    alignas(hardware_destructive_interference_size) Atomic<std::uint32_t> counter_{ 0 };
};

template <template <typename> typename Atomic>
serial_executor<Atomic>& inline_serial_executor() {
    static serial_executor<Atomic> impl{ inline_executor() };
    return impl;
}

} // namespace sl::exec::detail
