//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/model/concept.hpp"

#include <sl/meta/monad/maybe.hpp>

namespace sl::exec {

template <SomeSignal SignalT, typename ContinuationTV>
constexpr auto operator|(SignalT&& signal, ContinuationTV&& continuation) {
    return std::forward<ContinuationTV>(continuation)(std::move(signal));
}

template <typename ValueT, typename ErrorT>
void fulfill_slot(slot<ValueT, ErrorT>& slot, meta::maybe<meta::result<ValueT, ErrorT>> maybe_result) {
    if (!maybe_result.has_value()) {
        slot.cancel();
        return;
    }

    meta::result<ValueT, ErrorT> result = std::move(maybe_result).value();
    if (result.has_value()) {
        slot.set_value(std::move(result).value());
    } else {
        slot.set_error(std::move(result).error());
    }
}

} // namespace sl::exec
