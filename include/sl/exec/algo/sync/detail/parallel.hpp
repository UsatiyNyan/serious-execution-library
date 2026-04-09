//
// Created by usatiynyan.
// NOTE: parallel_connection breaks propagation of `try_cancel()`
//

#pragma once

#include "sl/exec/algo/sync/serial.hpp"
#include "sl/exec/model/connection.hpp"
#include "sl/exec/model/executor.hpp"
#include "sl/exec/thread/detail/polyfill.hpp"

#include <sl/meta/match/overloaded.hpp>
#include <sl/meta/monad/maybe.hpp>
#include <sl/meta/tuple/for_each.hpp>

#include <cstdint>
#include <limits>

namespace sl::exec::detail {

template <typename DeleteThisT, template <typename> typename Atomic, typename... ConnectionTs>
struct parallel_connection {
private:
    static constexpr std::size_t N = sizeof...(ConnectionTs);

    struct emit_task : task_node {
        emit_task(parallel_connection& self, std::array<cancel_handle*, N> cancel_handles)
            : cancel_handles_{ cancel_handles }, self_{ self } {}

        void execute() noexcept override { self_.serialized_emit(cancel_handles_); }
        void cancel() noexcept override {
            // FIXME: leaking for now, should be alright
        }

    private:
        std::array<cancel_handle*, N> cancel_handles_;
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
    cancel_handle& emit() && {
        std::array<cancel_handle*, N> cancel_handles{};
        {
            std::size_t i = 0;
            meta::for_each(
                [&](auto&& a_connection) { cancel_handles[i++] = &std::move(a_connection).emit(); },
                std::move(connections_)
            );
            DEBUG_ASSERT(i == N);
        }

        emit_impl(cancel_handles);
        return dummy_cancel_handle();
    };

    // all connections are subscribe_connection
    cancel_handle& emit_ordered() && {
        std::array<std::pair<connection*, std::uintptr_t>, N> ordered_connections{};
        {
            constexpr meta::overloaded get_ordering{
                [](const ordered_connection& an_ordered_connection) { return an_ordered_connection.get_ordering(); },
                [](const connection&) { return std::numeric_limits<std::uintptr_t>::max(); },
            };
            std::size_t i = 0;
            meta::for_each(
                [&](auto& a_subscribe_connection) {
                    ordered_connections[i++] = std::pair<connection*, std::uintptr_t>{
                        &a_subscribe_connection,
                        get_ordering(a_subscribe_connection.get_inner()),
                    };
                },
                connections_
            );
            DEBUG_ASSERT(i == N);
        }
        std::stable_sort(ordered_connections.begin(), ordered_connections.end(), [](const auto& x, const auto& y) {
            return x.second < y.second;
        });

        std::array<cancel_handle*, N> cancel_handles{};
        for (std::size_t i = 0; i < cancel_handles.size(); ++i) {
            cancel_handles[i] = &std::move(*ordered_connections[i].first).emit();
        }

        emit_impl(cancel_handles);
        return dummy_cancel_handle();
    }

private:
    void emit_impl(const std::array<cancel_handle*, N>& cancel_handles) {
        DEBUG_ASSERT(!tasks_.emit.has_value());
        executor_.schedule(tasks_.emit.emplace(*this, cancel_handles));
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
    static void serialized_try_cancel_beside_impl(
        const std::array<cancel_handle*, N>& cancel_handles,
        std::size_t excluded_index
    ) {
        for (std::size_t i = 0; i != cancel_handles.size(); ++i) {
            if (i == excluded_index) {
                continue;
            }
            ASSERT_VAL(cancel_handles[i])->try_cancel();
        }
    }

    void serialized_emit(const std::array<cancel_handle*, N>& cancel_handles) {
        state_.cancel_handles.emplace(cancel_handles);

        // can't touch tasks_.try_cancel or tasks_.try_cancel_beside
        if (state_.cancel_request.has_value()) {
            serialized_try_cancel_beside_impl(cancel_handles, state_.cancel_request.value());
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
        meta::maybe<std::array<cancel_handle*, N>> cancel_handles{};
        meta::maybe<std::size_t> cancel_request = {};
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
