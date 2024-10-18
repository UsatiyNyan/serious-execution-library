//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/conn/concept.hpp"
#include <utility>

namespace sl::exec {
namespace detail {

template <typename F>
struct [[nodiscard]] functor_slot {
    F functor;

    template <typename ResultTV>
    void set_result(ResultTV&& result) && {
        functor(std::forward<ResultTV>(result));
    }

    void cancel() && {}
};

} // namespace detail

template <typename FV>
auto as_slot(FV&& f) {
    using F = std::decay_t<FV>;
    return detail::functor_slot<F>{ .functor = std::forward<FV>(f) };
}

} // namespace sl::exec
