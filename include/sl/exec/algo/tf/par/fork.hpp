//
// Created by usatiynyan.
//
// value_type and error_type have to be thread-safe copyable
// auto [l_signal, r_signal] = signal | fork(); // this already calls .subscribe() on signal
//

#pragma once

#include "sl/exec/algo/emit/share.hpp"

namespace sl::exec {
namespace detail {

template <std::uint32_t N, template <typename> typename Atomic>
struct [[nodiscard]] fork {
    template <SomeSignal SignalT>
    constexpr auto operator()(SignalT&& signal) && {
        using value_type = typename SignalT::value_type;
        using error_type = typename SignalT::error_type;
        share_box<value_type, error_type> shared{ std::move(signal) };
        return make_signals(shared, std::make_integer_sequence<std::uint32_t, N>());
    }

private:
    template <typename ValueT, typename ErrorT, std::uint32_t... Idxs>
    static constexpr auto
        make_signals(share_box<ValueT, ErrorT>& shared, std::integer_sequence<std::uint32_t, Idxs...>) {
        using signal_type = detail::share_signal<ValueT, ErrorT, Atomic>;
        return std::array<signal_type, N>{ ((void)Idxs, shared.get_signal())... };
    }
};

template <template <typename> typename Atomic>
struct [[nodiscard]] fork_n {
    constexpr explicit fork_n(std::uint32_t count) : count_{ count } {}

    template <SomeSignal SignalT>
    constexpr auto operator()(SignalT&& signal) && {
        using value_type = typename SignalT::value_type;
        using error_type = typename SignalT::error_type;
        share_box<value_type, error_type> shared{ std::move(signal) };

        std::vector<detail::share_signal<value_type, error_type, Atomic>> signals;
        signals.reserve(count_);
        for (std::uint32_t i = 0; i < count_; ++i) {
            signals.emplace_back(shared.get_signal());
        }
        return signals;
    }

private:
    std::uint32_t count_;
};

} // namespace detail

template <std::uint32_t N = 2, template <typename> typename Atomic = detail::atomic>
constexpr auto fork() {
    return detail::fork<N, Atomic>{};
}

template <template <typename> typename Atomic = detail::atomic>
constexpr auto fork(std::uint32_t n) {
    return detail::fork_n<Atomic>{ n };
}

} // namespace sl::exec
