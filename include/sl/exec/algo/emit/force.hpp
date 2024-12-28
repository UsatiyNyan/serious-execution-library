//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/algo/tf/detail/transform_connection.hpp"
#include "sl/exec/model/concept.hpp"
#include <sl/meta/intrusive/forward_list.hpp>

#include <atomic>
#include <sl/meta/lifetime/defer.hpp>
#include <tl/optional.hpp>

namespace sl::exec {
namespace detail {

template <Signal SignalT>
struct [[nodiscard]] force_storage {
    using value_type = typename SignalT::value_type;
    using error_type = typename SignalT::error_type;
    using result_type = meta::result<value_type, error_type>;

    enum force_state : std::uintptr_t {
        empty = std::numeric_limits<std::uintptr_t>::min(),
        result = std::numeric_limits<std::uintptr_t>::max(),
    };

    struct force_slot : slot<value_type, error_type> {
        explicit force_slot(force_storage& self) : self_{ self } {}

        void set_value(value_type&& value) & override {
            self_.set_result(result_type{ tl::in_place, std::move(value) });
        }
        void set_error(error_type&& error) & override {
            self_.set_result(result_type{ tl::unexpect, std::move(error) });
        }
        void cancel() & override { self_.set_result(tl::nullopt); }

    private:
        force_storage& self_;
    };

public:
    explicit force_storage(SignalT&& signal) : connection_{ std::move(signal), force_slot{ *this } } {
        connection_.emit();
    }

    void set_result(tl::optional<result_type> result) {
        maybe_result_.emplace(std::move(result));

        std::uintptr_t state = state_.exchange(force_state::result, std::memory_order::acq_rel);
        if (state == force_state::empty) {
            return;
        }
        DEBUG_ASSERT(state != force_state::Result);
        auto* slot_ptr = reinterpret_cast<slot<value_type, error_type>*>(state);

        meta::defer cleanup{ [this] { delete this; } };

        fulfill_slot(*slot_ptr, std::move(maybe_result_).value());
    }

    void set_slot(slot<value_type, error_type>& slot) {
        std::uintptr_t expected = force_state::Empty;
        if (state_.load(std::memory_order::acquire) == expected
            && state_.compare_exchange_strong(
                expected,
                reinterpret_cast<std::uintptr_t>(&slot),
                std::memory_order::release,
                std::memory_order::acquire
            )) {
            return;
        }

        meta::defer cleanup{ [this] { delete this; } };

        fulfill_slot(slot, std::move(maybe_result_).value());
    }

private:
    transform_connection<SignalT, force_slot> connection_;
    tl::optional<tl::optional<result_type>> maybe_result_;
    std::atomic<std::uintptr_t> state_{ force_state::empty };
};

template <Signal SignalT>
struct force_connection {
    using value_type = typename SignalT::value_type;
    using error_type = typename SignalT::error_type;

    explicit force_connection(force_storage<SignalT>& storage, slot<value_type, error_type>& slot)
        : storage_{ storage }, slot_{ slot } {}

    void emit() & { storage_.set_slot(slot_); }

private:
    force_storage<SignalT>& storage_;
    slot<value_type, error_type>& slot_;
};

template <Signal SignalT>
struct [[nodiscard]] force_signal {
    using value_type = typename SignalT::value_type;
    using error_type = typename SignalT::error_type;

    explicit force_signal(SignalT&& signal)
        : executor_{ signal.get_executor() }, storage_{ new force_storage{ std::move(signal) } } {}

    Connection auto subscribe(slot<value_type, error_type>& slot) && {
        auto* storage_ptr = std::exchange(storage_, nullptr);
        DEBUG_ASSERT(storage_ptr != nullptr);
        return force_connection{
            /* .storage = */ *storage_ptr,
            /* .slot = */ slot,
        };
    }

    executor& get_executor() { return executor_; }

private:
    executor& executor_;
    force_storage<SignalT>* storage_;
};

struct [[nodiscard]] force {
    template <Signal SignalT>
    constexpr Signal auto operator()(SignalT&& signal) && {
        return force_signal<SignalT>{
            /* .signal = */ std::move(signal),
        };
    }
};

} // namespace detail

constexpr auto force() { return detail::force{}; }

} // namespace sl::exec
