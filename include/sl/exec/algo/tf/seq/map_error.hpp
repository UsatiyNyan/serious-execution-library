//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/algo/tf/detail/transform_connection.hpp"

#include "sl/exec/model/concept.hpp"
#include "sl/exec/model/executor.hpp"

#include <tl/optional.hpp>

namespace sl::exec {
namespace detail {

template <typename ValueT, typename InputErrorT, typename ErrorT, typename F>
struct [[nodiscard]] map_error_slot : slot<ValueT, InputErrorT> {
    struct map_error_task : task_node {
        explicit map_error_task(map_error_slot& self) : self_{ self } {}

        void execute() noexcept override {
            if (!ASSUME_VAL(self_.maybe_error_.has_value())) {
                return;
            }
            auto error = self_.functor_(std::move(self_.maybe_error_).value());
            self_.slot_.set_error(std::move(error));
        }
        void cancel() noexcept override { self_.cancel(); };

    private:
        map_error_slot& self_;
    };

    map_error_slot(F&& functor, slot<ValueT, ErrorT>& slot, executor& executor)
        : functor_{ std::move(functor) }, slot_{ slot }, executor_{ executor } {}

    void set_value(ValueT&& value) & override { slot_.set_value(std::move(value)); }
    void set_error(InputErrorT&& error) & override {
        maybe_error_.emplace(std::move(error));
        auto& task = maybe_task_.emplace(*this);
        executor_.schedule(&task);
    }
    void cancel() & override { slot_.cancel(); }

private:
    F functor_;
    ::tl::optional<InputErrorT> maybe_error_{};
    ::tl::optional<map_error_task> maybe_task_{};
    slot<ValueT, ErrorT>& slot_;
    executor& executor_;
};

template <Signal SignalT, typename F>
struct [[nodiscard]] map_error_signal {
    using value_type = typename SignalT::value_type;
    using error_type = std::invoke_result_t<F, typename SignalT::error_type>;

    SignalT signal;
    F functor;

    Connection auto subscribe(slot<value_type, error_type>& slot) && {
        return transform_connection{
            /* .signal = */ std::move(signal),
            /* .slot = */
            map_error_slot<value_type, typename SignalT::error_type, error_type, F>{
                /* .functor = */ std::move(functor),
                /* .slot = */ slot,
                /* .executor = */ get_executor(),
            },
        };
    }

    executor& get_executor() { return signal.get_executor(); }
};

template <typename F>
struct [[nodiscard]] map_error {
    F functor;

    template <Signal SignalT>
    constexpr auto operator()(SignalT&& signal) && {
        return map_error_signal<SignalT, F>{
            .signal = std::move(signal),
            .functor = std::move(functor),
        };
    }
};

} // namespace detail

template <typename FV>
constexpr auto map_error(FV&& functor) {
    using F = std::decay_t<FV>;
    return detail::map_error<F>{ .functor = std::forward<FV>(functor) };
}

} // namespace sl::exec
