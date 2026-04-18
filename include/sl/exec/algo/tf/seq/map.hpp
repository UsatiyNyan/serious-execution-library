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
struct [[nodiscard]] map_slot final {
    struct map_task final : task_node {
        explicit map_task(map_slot& self) : self_{ self } {}

        void execute() noexcept override {
            if (!ASSERT_VAL(self_.maybe_value_.has_value())) {
                return;
            }
            auto value = self_.functor_(std::move(self_.maybe_value_).value());
            std::move(self_.slot_).set_value(std::move(value));
        }
        void cancel() noexcept override { std::move(self_).set_null(); }

    private:
        map_slot& self_;
    };

    F functor_;
    SlotT slot_;
    executor& executor_;
    meta::maybe<InputValueT> maybe_value_{};
    meta::maybe<map_task> maybe_task_{};

    void set_value(InputValueT&& value) && noexcept {
        maybe_value_.emplace(std::move(value));
        auto& task = maybe_task_.emplace(*this);
        executor_.schedule(task);
    }
    void set_error(ErrorT&& error) && noexcept { std::move(slot_).set_error(std::move(error)); }
    void set_null() && noexcept { std::move(slot_).set_null(); }
};

template <SomeSignal SignalT, typename F, typename SlotCtorT>
struct [[nodiscard]] map_connection final {
    using input_value_type = typename SignalT::value_type;
    using value_type = std::invoke_result_t<F, input_value_type>;
    using error_type = typename SignalT::error_type;
    using SlotT = SlotFrom<SlotCtorT>;
    using map_slot_type = map_slot<input_value_type, value_type, error_type, F, SlotT>;

    struct map_slot_ctor {
        F functor;
        SlotCtorT slot_ctor;
        executor& ex;

        constexpr map_slot_type operator()() && noexcept {
            return map_slot_type{
                .functor_ = std::move(functor),
                .slot_ = std::move(slot_ctor)(),
                .executor_ = ex,
            };
        }
    };

    ConnectionFor<SignalT, map_slot_ctor> connection;

    constexpr CancelHandle auto emit() && noexcept { return std::move(connection).emit(); }
};

template <SomeSignal SignalT, typename F>
struct [[nodiscard]] map_signal final {
    using value_type = std::invoke_result_t<F, typename SignalT::value_type>;
    using error_type = typename SignalT::error_type;

public:
    constexpr map_signal(SignalT&& signal, F&& functor)
        : signal_{ std::move(signal) }, functor_{ std::move(functor) } {}

    template <SlotCtor<value_type, error_type> SlotCtorT>
    constexpr Connection auto subscribe(SlotCtorT&& slot_ctor) && noexcept {
        using ConnectionT = map_connection<SignalT, F, SlotCtorT>;
        using SlotCtorForSignal = typename ConnectionT::map_slot_ctor;
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
struct [[nodiscard]] map final {
    constexpr explicit map(F&& functor) : functor_{ std::move(functor) } {}

    template <SomeSignal SignalT>
    constexpr SomeSignal auto operator()(SignalT&& signal) && noexcept {
        return map_signal<SignalT, F>{ std::move(signal), std::move(functor_) };
    }

private:
    F functor_;
};

} // namespace detail

template <typename F>
constexpr auto map(F functor) noexcept {
    return detail::map<F>{ std::move(functor) };
}

} // namespace sl::exec
