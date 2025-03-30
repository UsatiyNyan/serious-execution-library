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

template <std::uint32_t N>
struct [[nodiscard]] fork {
    template <Signal SignalT>
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
        return std::array<detail::share_signal<ValueT, ErrorT>, N>{ ((void)Idxs, shared.get_signal())... };
    }
};

struct [[nodiscard]] fork_n {
    constexpr explicit fork_n(std::uint32_t count) : count_{ count } {}

    template <Signal SignalT>
    constexpr auto operator()(SignalT&& signal) && {
        using value_type = typename SignalT::value_type;
        using error_type = typename SignalT::error_type;
        share_box<value_type, error_type> shared{ std::move(signal) };

        std::vector<detail::share_signal<value_type, error_type>> signals;
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

template <std::uint32_t N = 2>
constexpr auto fork() {
    return detail::fork<N>{};
}

constexpr auto fork(std::uint32_t n) { return detail::fork_n{ n }; }

} // namespace sl::exec
