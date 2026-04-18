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
struct [[nodiscard]] map_error_slot final {
    struct map_error_task final : task_node {
        explicit map_error_task(map_error_slot& self) : self_{ self } {}

        void execute() noexcept override {
            if (!ASSERT_VAL(self_.maybe_error_.has_value())) {
                return;
            }
            auto error = self_.functor_(std::move(self_.maybe_error_).value());
            std::move(self_.slot_).set_error(std::move(error));
        }
        void cancel() noexcept override { std::move(self_).set_null(); }

    private:
        map_error_slot& self_;
    };

    F functor_;
    SlotT slot_;
    executor& executor_;
    meta::maybe<InputErrorT> maybe_error_{};
    meta::maybe<map_error_task> maybe_task_{};

    void set_value(ValueT&& value) && noexcept { std::move(slot_).set_value(std::move(value)); }
    void set_error(InputErrorT&& error) && noexcept {
        maybe_error_.emplace(std::move(error));
        auto& task = maybe_task_.emplace(*this);
        executor_.schedule(task);
    }
    void set_null() && noexcept { std::move(slot_).set_null(); }
};

template <SomeSignal SignalT, typename F, typename SlotCtorT>
struct [[nodiscard]] map_error_connection final {
    using value_type = typename SignalT::value_type;
    using input_error_type = typename SignalT::error_type;
    using error_type = std::invoke_result_t<F, input_error_type>;
    using SlotT = SlotFrom<SlotCtorT>;
    using map_error_slot_type = map_error_slot<value_type, input_error_type, error_type, F, SlotT>;

    struct map_error_slot_ctor {
        F functor;
        SlotCtorT slot_ctor;
        executor& ex;

        constexpr map_error_slot_type operator()() && noexcept {
            return map_error_slot_type{
                .functor_ = std::move(functor),
                .slot_ = std::move(slot_ctor)(),
                .executor_ = ex,
            };
        }
    };

    ConnectionFor<SignalT, map_error_slot_ctor> connection;

    constexpr CancelHandle auto emit() && noexcept { return std::move(connection).emit(); }
};

template <SomeSignal SignalT, typename F>
struct [[nodiscard]] map_error_signal final {
    using value_type = typename SignalT::value_type;
    using error_type = std::invoke_result_t<F, typename SignalT::error_type>;

public:
    constexpr map_error_signal(SignalT&& signal, F&& functor)
        : signal_{ std::move(signal) }, functor_{ std::move(functor) } {}

    template <SlotCtor<value_type, error_type> SlotCtorT>
    constexpr Connection auto subscribe(SlotCtorT slot_ctor) && noexcept {
        using ConnectionT = map_error_connection<SignalT, F, SlotCtorT>;
        using SlotCtorForSignal = typename ConnectionT::map_error_slot_ctor;
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
struct [[nodiscard]] map_error final {
    constexpr explicit map_error(F&& functor) : functor_{ std::move(functor) } {}

    template <SomeSignal SignalT>
    constexpr SomeSignal auto operator()(SignalT&& signal) && noexcept {
        return map_error_signal<SignalT, F>{ std::move(signal), std::move(functor_) };
    }

private:
    F functor_;
};

} // namespace detail

template <typename F>
constexpr auto map_error(F functor) noexcept {
    return detail::map_error<F>{ std::move(functor) };
}

} // namespace sl::exec
