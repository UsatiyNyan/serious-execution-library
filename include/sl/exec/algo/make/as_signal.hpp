//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/model/concept.hpp"

namespace sl::exec {
namespace detail {

template <typename T>
struct as_signal;

} // namespace detail

template <typename TV>
constexpr SomeSignal auto as_signal(TV&& x) {
    using T = std::decay_t<TV>;
    return detail::as_signal<T>::call(std::forward<TV>(x));
}

} // namespace sl::exec
