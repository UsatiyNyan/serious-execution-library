//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/algo/make/result.hpp"
#include "sl/exec/algo/sched/continue_on.hpp"
#include "sl/exec/algo/sched/start_on.hpp"
#include "sl/exec/algo/sync/serial.hpp"

#include "sl/exec/thread/detail/atomic.hpp"
#include <sl/meta/traits/unique.hpp>

namespace sl::exec {

struct [[nodiscard]] mutex_lock : meta::unique {
    constexpr explicit mutex_lock(executor& executor) : executor_{ executor } {}
    mutex_lock(mutex_lock&& other) noexcept
        : executor_{ other.executor_ }, is_locked_{ std::exchange(other.is_locked_, false) } {}
    ~mutex_lock() noexcept { ASSERT(!is_locked_); }

    constexpr Signal<meta::unit, meta::undefined> auto unlock() && {
        is_locked_ = false;
        return start_on(executor_);
    }
    constexpr Signal<meta::unit, meta::undefined> auto unlock_on(executor& executor) && {
        is_locked_ = false;
        return start_on(executor);
    }

private:
    executor& executor_;
    bool is_locked_ = true;
};

template <template <typename> typename Atomic = detail::atomic>
struct [[nodiscard]] mutex {
    constexpr explicit mutex(executor& executor) : serial_executor_{ executor } {}

    constexpr Signal<mutex_lock, meta::undefined> auto lock() {
        return value_as_signal(mutex_lock{ serial_executor_.get_inner() }) | continue_on(serial_executor_);
    }

private:
    serial_executor<Atomic> serial_executor_;
};

} // namespace sl::exec
