//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/model/concept.hpp"
#include "sl/exec/model/executor.hpp"

#include <sl/meta/assert.hpp>
#include <sl/meta/monad/maybe.hpp>

namespace sl::exec {
namespace detail {

template <typename InputValueT, typename ValueT, typename ErrorT, typename F, typename SlotT>
struct [[nodiscard]] and_then_slot final {
    struct and_then_task final : task_node {
        explicit and_then_task(and_then_slot& self) : self_{ self } {}

        void execute() noexcept override {
            if (!ASSERT_VAL(self_.maybe_value_.has_value())) {
                return;
            }
            auto result = self_.functor_(std::move(self_.maybe_value_).value());
            if (result.has_value()) {
                std::move(self_.slot_).set_value(std::move(result).value());
            } else {
                std::move(self_.slot_).set_error(std::move(result).error());
            }
        }
        void cancel() noexcept override { std::move(self_).set_null(); }

    private:
        and_then_slot& self_;
    };

    F functor_;
    SlotT slot_;
    executor& executor_;
    meta::maybe<InputValueT> maybe_value_{};
    meta::maybe<and_then_task> maybe_task_{};

    void set_value(InputValueT&& value) && noexcept {
        maybe_value_.emplace(std::move(value));
        auto& task = maybe_task_.emplace(*this);
        executor_.schedule(task);
    }
    void set_error(ErrorT&& error) && noexcept { std::move(slot_).set_error(std::move(error)); }
    void set_null() && noexcept { std::move(slot_).set_null(); }
};

template <SomeSignal SignalT, typename F, typename SlotCtorT, typename ResultT = std::invoke_result_t<F, typename SignalT::value_type>>
    requires std::same_as<typename SignalT::error_type, typename ResultT::error_type>
struct [[nodiscard]] and_then_connection final {
    using input_value_type = typename SignalT::value_type;
    using value_type = typename ResultT::value_type;
    using error_type = typename ResultT::error_type;
    using SlotT = SlotFrom<SlotCtorT>;
    using and_then_slot_type = and_then_slot<input_value_type, value_type, error_type, F, SlotT>;

    struct and_then_slot_ctor {
        F functor;
        SlotCtorT slot_ctor;
        executor& ex;

        constexpr and_then_slot_type operator()() && noexcept {
            return and_then_slot_type{
                .functor_ = std::move(functor),
                .slot_ = std::move(slot_ctor)(),
                .executor_ = ex,
            };
        }
    };

    ConnectionFor<SignalT, and_then_slot_ctor> connection;

    constexpr CancelHandle auto emit() && noexcept { return std::move(connection).emit(); }
};

template <SomeSignal SignalT, typename F, typename ResultT = std::invoke_result_t<F, typename SignalT::value_type>>
    requires std::same_as<typename SignalT::error_type, typename ResultT::error_type>
struct [[nodiscard]] and_then_signal final {
    using value_type = typename ResultT::value_type;
    using error_type = typename ResultT::error_type;

public:
    constexpr and_then_signal(SignalT&& signal, F&& functor)
        : signal_{ std::move(signal) }, functor_{ std::move(functor) } {}

    template <SlotCtor<value_type, error_type> SlotCtorT>
    constexpr Connection auto subscribe(SlotCtorT&& slot_ctor) && noexcept {
        using ConnectionT = and_then_connection<SignalT, F, SlotCtorT>;
        using SlotCtorForSignal = typename ConnectionT::and_then_slot_ctor;
        executor& ex = signal_.get_executor();
        return ConnectionT{
            .connection = std::move(signal_).subscribe(
                SlotCtorForSignal{ std::move(functor_), std::move(slot_ctor), ex }
            ),
        };
    }

    constexpr executor& get_executor() noexcept { return signal_.get_executor(); }

private:
    SignalT signal_;
    F functor_;
};

template <typename F>
struct [[nodiscard]] and_then final {
    constexpr explicit and_then(F&& functor) : functor_{ std::move(functor) } {}

    template <SomeSignal SignalT>
    constexpr SomeSignal auto operator()(SignalT&& signal) && noexcept {
        return and_then_signal<SignalT, F>{ std::move(signal), std::move(functor_) };
    }

private:
    F functor_;
};

} // namespace detail

template <typename F>
constexpr auto and_then(F functor) noexcept {
    return detail::and_then<F>{ std::move(functor) };
}

} // namespace sl::exec
