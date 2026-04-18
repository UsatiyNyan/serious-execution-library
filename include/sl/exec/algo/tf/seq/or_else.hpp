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

template <typename ValueT, typename InputErrorT, typename ErrorT, typename F, typename SlotT>
struct [[nodiscard]] or_else_slot final {
    struct or_else_task final : task_node {
        explicit or_else_task(or_else_slot& self) : self_{ self } {}

        void execute() noexcept override {
            if (!ASSERT_VAL(self_.maybe_error_.has_value())) {
                return;
            }
            auto result = self_.functor_(std::move(self_.maybe_error_).value());
            if (result.has_value()) {
                std::move(self_.slot_).set_value(std::move(result).value());
            } else {
                std::move(self_.slot_).set_error(std::move(result).error());
            }
        }
        void cancel() noexcept override { std::move(self_).set_null(); }

    private:
        or_else_slot& self_;
    };

    F functor_;
    SlotT slot_;
    executor& executor_;
    meta::maybe<InputErrorT> maybe_error_{};
    meta::maybe<or_else_task> maybe_task_{};

    void set_value(ValueT&& value) && noexcept { std::move(slot_).set_value(std::move(value)); }
    void set_error(InputErrorT&& error) && noexcept {
        maybe_error_.emplace(std::move(error));
        auto& task = maybe_task_.emplace(*this);
        executor_.schedule(task);
    }
    void set_null() && noexcept { std::move(slot_).set_null(); }
};

template <SomeSignal SignalT, typename F, typename SlotCtorT, typename ResultT = std::invoke_result_t<F, typename SignalT::error_type>>
    requires std::same_as<typename SignalT::value_type, typename ResultT::value_type>
struct [[nodiscard]] or_else_connection final {
    using value_type = typename ResultT::value_type;
    using input_error_type = typename SignalT::error_type;
    using error_type = typename ResultT::error_type;
    using SlotT = SlotFrom<SlotCtorT>;
    using or_else_slot_type = or_else_slot<value_type, input_error_type, error_type, F, SlotT>;

    struct or_else_slot_ctor {
        F functor;
        SlotCtorT slot_ctor;
        executor& ex;

        constexpr or_else_slot_type operator()() && noexcept {
            return or_else_slot_type{
                .functor_ = std::move(functor),
                .slot_ = std::move(slot_ctor)(),
                .executor_ = ex,
            };
        }
    };

    ConnectionFor<SignalT, or_else_slot_ctor> connection;

    constexpr CancelHandle auto emit() && noexcept { return std::move(connection).emit(); }
};

template <SomeSignal SignalT, typename F, typename ResultT = std::invoke_result_t<F, typename SignalT::error_type>>
    requires std::same_as<typename SignalT::value_type, typename ResultT::value_type>
struct [[nodiscard]] or_else_signal final {
    using value_type = typename ResultT::value_type;
    using error_type = typename ResultT::error_type;

public:
    constexpr or_else_signal(SignalT&& signal, F&& functor)
        : signal_{ std::move(signal) }, functor_{ std::move(functor) } {}

    template <SlotCtor<value_type, error_type> SlotCtorT>
    constexpr Connection auto subscribe(SlotCtorT&& slot_ctor) && noexcept {
        using ConnectionT = or_else_connection<SignalT, F, SlotCtorT>;
        using SlotCtorForSignal = typename ConnectionT::or_else_slot_ctor;
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
struct [[nodiscard]] or_else final {
    constexpr explicit or_else(F&& functor) : functor_{ std::move(functor) } {}

    template <SomeSignal SignalT>
    constexpr SomeSignal auto operator()(SignalT&& signal) && noexcept {
        return or_else_signal<SignalT, F>{ std::move(signal), std::move(functor_) };
    }

private:
    F functor_;
};

} // namespace detail

template <typename F>
constexpr auto or_else(F functor) noexcept {
    return detail::or_else<F>{ std::move(functor) };
}

} // namespace sl::exec
