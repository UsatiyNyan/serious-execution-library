//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/model/concept.hpp"

namespace sl::exec {
namespace detail {

template <typename F, typename SlotCtorT>
struct [[nodiscard]] schedule_connection final : task_node {
    constexpr schedule_connection(F&& f, SlotCtorT&& slot_ctor, executor& ex) noexcept
        : f_(std::move(f)), slot_(std::move(slot_ctor)()), ex_(ex) {}

    CancelHandle auto emit() && noexcept {
        ex_.schedule(*this);
        return dummy_cancel_handle{};
    }

    void execute() noexcept override {
        auto result = f_();
        if (result.has_value()) {
            std::move(slot_).set_value(std::move(result).value());
        } else {
            std::move(slot_).set_error(std::move(result).error());
        }
    }
    void cancel() noexcept override { std::move(slot_).set_null(); }

private:
    F f_;
    SlotFrom<SlotCtorT> slot_;
    executor& ex_;
};

template <typename F>
struct [[nodiscard]] schedule_signal final {
    using result_type = std::invoke_result_t<F>;
    using value_type = typename result_type::value_type;
    using error_type = typename result_type::error_type;

    F f;
    executor& ex;

public:
    template <SlotCtor<value_type, error_type> SlotCtorT>
    constexpr Connection auto subscribe(SlotCtorT&& slot_ctor) && noexcept {
        return schedule_connection<F, SlotCtorT>{
            std::move(f),
            std::move(slot_ctor),
            ex,
        };
    }

    static executor& get_executor() noexcept { return inline_executor(); }
};

} // namespace detail

template <typename FV>
constexpr SomeSignal auto schedule(executor& an_executor, FV&& f) {
    using F = std::decay_t<FV>;
    return detail::schedule_signal<F>{
        .f = std::forward<FV>(f),
        .ex = an_executor,
    };
}

} // namespace sl::exec
