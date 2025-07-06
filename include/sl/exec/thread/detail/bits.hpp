//
// Created by usatiynyan.
//

#pragma once

namespace sl::exec::detail::bits {

template <typename T>
constexpr T fill_ones(T width) {
    return (T{ 1 } << width) - T{ 1 };
}

} // namespace sl::exec::detail::bits
