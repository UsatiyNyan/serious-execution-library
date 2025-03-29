//
// Created by usatiynyan.
//
// value_type and error_type have to be thread-safe copyable
// auto [l_signal, r_signal] = signal | fork(); // this already calls .subscribe() on signal
//

#pragma once

#include "sl/exec/algo/emit/subscribe.hpp"
#include "sl/exec/model/concept.hpp"
#include "sl/exec/model/syntax.hpp"
#include "sl/exec/thread/detail/polyfill.hpp"

#include <sl/meta/intrusive/algorithm.hpp>
#include <sl/meta/lifetime/defer.hpp>
#include <sl/meta/lifetime/finalizer.hpp>
#include <sl/meta/lifetime/immovable.hpp>
#include <sl/meta/monad/maybe.hpp>
#include <sl/meta/type/list.hpp>

#include <atomic>
#include <utility>

namespace sl::exec {
namespace detail {

template <Signal SignalT>
struct fork_connection {
    using value_type = typename SignalT::value_type;
    using error_type = typename SignalT::error_type;
    using result_type = meta::result<value_type, error_type>;

    enum fork_state : std::uintptr_t {
        fork_state_empty = std::numeric_limits<std::uintptr_t>::min(),
        fork_state_result = std::numeric_limits<std::uintptr_t>::max(),
    };

    struct fork_slot : slot<value_type, error_type> {
        explicit fork_slot(fork_connection& self) : self_{ self } {}

        void set_value(value_type&& value) & override {
            self_.set_result(result_type{ tl::in_place, std::move(value) });
        }
        void set_error(error_type&& error) & override {
            self_.set_result(result_type{ tl::unexpect, std::move(error) });
        }
        void cancel() & override { self_.set_result(meta::null); }

    private:
        fork_connection& self_;
    };

public:
    // call subscribe on signal early to not have to deal with it later
    fork_connection(SignalT&& signal, std::uint32_t count)
        : connection_{
              /* .signal = */ std::move(signal),
              /* .slot = */ fork_slot{ *this },
          },
          counter_{ count } {}

    void decref(std::uint32_t count = 1) & {
        if (counter_.fetch_sub(count, std::memory_order::relaxed) == count) {
            delete this;
        }
    }

    static void try_decref(fork_connection* maybe_self) {
        if (maybe_self != nullptr) {
            maybe_self->decref();
        }
    }

    void emit(exec::slot_node<value_type, error_type>& slot_node) && {
        std::uintptr_t state = fork_state_empty;
        if (state_.compare_exchange_strong(
                state,
                reinterpret_cast<std::uintptr_t>(&slot_node),
                std::memory_order::release,
                std::memory_order::acquire
            )) {
            std::move(connection_).emit();
            return;
        }

        do {
            slot_node.intrusive_next = reinterpret_cast<exec::slot_node<value_type, error_type>*>(state);
        } while (state != fork_state_result
                 && !state_.compare_exchange_weak(
                     state,
                     reinterpret_cast<std::uintptr_t>(&slot_node),
                     std::memory_order::release,
                     std::memory_order::acquire
                 ));

        if (state == fork_state_result) {
            meta::defer cleanup{ [this] { decref(); } };
            // exlicit copy, unfortunately
            fulfill_slot(slot_node, meta::maybe<result_type>{ maybe_result_ });
        }
    }

private:
    void set_result(meta::maybe<result_type> maybe_result) {
        maybe_result_ = std::move(maybe_result);

        std::uintptr_t state = state_.exchange(fork_state_result, std::memory_order::acq_rel);
        const bool state_is_empty = state == fork_state_empty;
        ASSUME(!state_is_empty, "set_result before emit, since this is lazy impl");
        if (state_is_empty) {
            return;
        }
        DEBUG_ASSERT(state != fork_state_result);

        auto* slot_list = reinterpret_cast<exec::slot_node<value_type, error_type>*>(state);
        const std::uint32_t slot_list_count = [slot_list] {
            std::uint32_t counter = 0;
            meta::intrusive_forward_list_node_for_each(slot_list, [&counter](exec::slot_node<value_type, error_type>*) {
                ++counter;
            });
            return counter;
        }();

        meta::defer cleanup{ [this, slot_list_count] { decref(slot_list_count); } };
        meta::intrusive_forward_list_node_for_each(
            slot_list,
            [this](exec::slot_node<value_type, error_type>* slot_node_ptr) {
                // exlicit copy, unfortunately
                fulfill_slot(*slot_node_ptr, meta::maybe<result_type>{ maybe_result_ });
            }
        );
    }

private:
    subscribe_connection<SignalT, fork_slot> connection_;
    meta::maybe<result_type> maybe_result_{};
    alignas(hardware_destructive_interference_size) std::atomic<std::uint32_t> counter_;
    alignas(hardware_destructive_interference_size) std::atomic<std::uintptr_t> state_{ fork_state_empty };
};

template <Signal SignalT>
struct [[nodiscard]] fork_connection_box : meta::immovable {
    using value_type = typename SignalT::value_type;
    using error_type = typename SignalT::error_type;

public:
    fork_connection_box(fork_connection<SignalT>* connection_ptr, slot<value_type, error_type>& slot)
        : slot_node_{ slot }, connection_ptr_{ connection_ptr } {}
    ~fork_connection_box() { fork_connection<SignalT>::try_decref(connection_ptr_); }

    void emit() && {
        auto& connection = *DEBUG_ASSERT_VAL(std::exchange(connection_ptr_, nullptr));
        std::move(connection).emit(slot_node_);
    }

private:
    exec::slot_node<value_type, error_type> slot_node_;
    fork_connection<SignalT>* connection_ptr_;
};

template <Signal SignalT>
struct [[nodiscard]] fork_signal : meta::finalizer<fork_signal<SignalT>> {
    using value_type = typename SignalT::value_type;
    using error_type = typename SignalT::error_type;

    fork_signal(fork_connection<SignalT>* connection_ptr, executor& executor)
        : meta::finalizer<fork_signal<SignalT>>{ [](fork_signal& self) {
              fork_connection<SignalT>::try_decref(self.connection_ptr_);
          } },
          connection_ptr_{ connection_ptr }, executor_{ executor } {}

    Connection auto subscribe(slot<value_type, error_type>& slot) && {
        return fork_connection_box{
            /* .connection_ptr= */ DEBUG_ASSERT_VAL(std::exchange(connection_ptr_, nullptr)),
            /* .slot= */ slot,
        };
    }

    executor& get_executor() & { return executor_; }

private:
    fork_connection<SignalT>* connection_ptr_;
    executor& executor_;
};

template <std::uint32_t N>
struct [[nodiscard]] fork {
    template <Signal SignalT>
    constexpr auto operator()(SignalT&& signal) && {
        executor& executor = signal.get_executor();
        auto* connection_ptr = new fork_connection<SignalT>{
            /* .signal = */ std::move(signal),
            /* .count = */ N,
        };
        return make_signals(connection_ptr, executor, std::make_integer_sequence<std::uint32_t, N>());
    }

private:
    template <Signal SignalT, std::uint32_t... Idxs>
    static constexpr auto
        make_signals(fork_connection<SignalT>* connection_ptr, executor& executor, std::integer_sequence<std::uint32_t, Idxs...>) {
        return std::make_tuple(
            ((void)Idxs,
             fork_signal<SignalT>{
                 /* .connection_ptr = */ connection_ptr,
                 /* .executor = */ executor,
             })...
        );
    }
};

} // namespace detail

template <std::uint32_t N = 2>
constexpr auto fork() {
    return detail::fork<N>{};
}

} // namespace sl::exec
