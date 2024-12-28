//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/model/concept.hpp"
#include "sl/exec/model/executor.hpp"

#include "sl/exec/algo/tf/detail/transform_connection.hpp"
#include "sl/exec/model/syntax.hpp"

#include <tl/optional.hpp>

namespace sl::exec {
namespace detail {

template <typename ValueT, typename ErrorT>
struct [[nodiscard]] on_slot : slot<ValueT, ErrorT> {
    struct on_task : task_node {
        explicit on_task(on_slot& self) : self_{ self } {}

        void execute() noexcept override {
            DEBUG_ASSERT(self_.maybe_result_.has_value());
            fulfill_slot(self_.slot_, self_.maybe_result_);
        }
        void cancel() noexcept override { self_.cancel(); };

    private:
        on_slot& self_;
    };

    on_slot(slot<ValueT, ErrorT>& slot, executor& executor) : slot_{ slot }, executor_{ executor } {}

    void set_value(ValueT&& value) & override {
        maybe_result_.emplace(tl::in_place, std::move(value));
        auto& task = maybe_task_.emplace(*this);
        executor_.schedule(&task);
    }
    void set_error(ErrorT&& error) & override {
        maybe_result_.emplace(tl::unexpect, std::move(error));
        auto& task = maybe_task_.emplace(*this);
        executor_.schedule(&task);
    }
    void cancel() & override { slot_.cancel(); }

private:
    tl::optional<meta::result<ValueT, ErrorT>> maybe_result_{};
    tl::optional<on_task> maybe_task_{};
    slot<ValueT, ErrorT>& slot_;
    executor& executor_;
};

template <Signal SignalT>
struct [[nodiscard]] on_signal {
    using value_type = typename SignalT::value_type;
    using error_type = typename SignalT::error_type;

    on_signal(SignalT&& signal, executor& executor) : signal_{ std::move(signal) }, executor_{ executor } {}

    Connection auto subscribe(slot<value_type, error_type>& slot) && {
        return transform_connection{
            /* .signal = */ std::move(signal_),
            /* .slot = */
            on_slot<value_type, error_type>{
                /* .slot = */ slot,
                /* .executor = */ executor_,
            },
        };
    }

    executor& get_executor() { return executor_; }

private:
    SignalT signal_;
    executor& executor_;
};

struct [[nodiscard]] on {
    executor& executor;

    template <Signal SignalT>
    constexpr Signal auto operator()(SignalT&& signal) && {
        return on_signal<SignalT>{
            /* .signal = */ std::move(signal),
            /* .executor = */ executor,
        };
    }
};

} // namespace detail


constexpr auto on(executor& executor) {
    return detail::on{
        .executor = executor,
    };
}

} // namespace sl::exec
