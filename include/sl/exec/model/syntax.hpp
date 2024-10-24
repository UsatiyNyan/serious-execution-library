//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/model/concept.hpp"

namespace sl::exec {

template <Signal SignalT, typename ContinuationTV>
constexpr auto operator|(SignalT&& signal, ContinuationTV&& continuation) {
    return std::forward<ContinuationTV>(continuation)(std::move(signal));
}

} // namespace sl::exec
