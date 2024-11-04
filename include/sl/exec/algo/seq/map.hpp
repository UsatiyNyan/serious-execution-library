//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/algo/seq/transform_connection.hpp"

#include "sl/exec/model/concept.hpp"
#include "sl/exec/model/executor.hpp"

#include <tl/optional.hpp>

namespace sl::exec {
namespace detail {

template <typename InputValueT, typename ValueT, typename ErrorT, typename F>
struct [[nodiscard]] map_slot : slot<InputValueT, ErrorT> {
    struct map_task : task_node {
        explicit map_task(map_slot& self) : self_{ self } {}

        void execute() noexcept override {
            if (!ASSUME_VAL(self_.maybe_value_.has_value())) {
                return;
            }
            auto value = self_.functor_(std::move(self_.maybe_value_).value());
            self_.slot_.set_value(std::move(value));
        }
        void cancel() noexcept override { self_.cancel(); };

    private:
        map_slot& self_;
    };

    map_slot(F&& functor, slot<ValueT, ErrorT>& slot, executor& executor)
        : functor_{ std::move(functor) }, slot_{ slot }, executor_{ executor } {}

    void set_value(InputValueT&& value) & override {
        maybe_value_.emplace(std::move(value));
        auto& task = maybe_task_.emplace(*this);
        executor_.schedule(&task);
    }
    void set_error(ErrorT&& error) & override { slot_.set_error(std::move(error)); }
    void cancel() & override { slot_.cancel(); }

private:
    F functor_;
    ::tl::optional<InputValueT> maybe_value_{};
    ::tl::optional<map_task> maybe_task_{};
    slot<ValueT, ErrorT>& slot_;
    executor& executor_;
};

template <Signal SignalT, typename F>
struct [[nodiscard]] map_signal {
    using value_type = std::invoke_result_t<F, typename SignalT::value_type>;
    using error_type = typename SignalT::error_type;

    SignalT signal;
    F functor;

    Connection auto subscribe(slot<value_type, error_type>& slot) && {
        return transform_connection{
            /* .signal = */ std::move(signal),
            /* .slot = */
            map_slot<typename SignalT::value_type, value_type, error_type, F>{
                /* .functor = */ std::move(functor),
                /* .slot = */ slot,
                /* .executor = */ get_executor(),
            },
        };
    }

    executor& get_executor() { return signal.get_executor(); }
};

template <typename F>
struct [[nodiscard]] map {
    F functor;

    template <Signal SignalT>
    constexpr auto operator()(SignalT&& signal) && {
        return map_signal<SignalT, F>{
            .signal = std::move(signal),
            .functor = std::move(functor),
        };
    }
};

} // namespace detail

template <typename FV>
constexpr auto map(FV&& functor) {
    using F = std::decay_t<FV>;
    return detail::map<F>{ .functor = std::forward<FV>(functor) };
}

} // namespace sl::exec
