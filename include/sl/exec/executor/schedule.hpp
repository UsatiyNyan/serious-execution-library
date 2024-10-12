//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/executor/executor.hpp"

namespace sl::exec {
namespace detail {

template <typename T>
struct schedule {
    template <typename TV>
    static void impl(executor&, TV&&) noexcept; // would not compile if there's no suitable specification
};

} // namespace detail

template <typename TV>
void schedule(executor& executor, TV&& value) {
    using T = std::decay_t<TV>;
    detail::schedule<T>::impl(executor, std::forward<TV>(value));
}

} // namespace sl::exec
