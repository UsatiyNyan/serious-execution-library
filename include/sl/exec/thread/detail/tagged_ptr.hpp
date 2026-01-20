//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/thread/detail/bits.hpp"

#include <sl/meta/assert.hpp>

#include <bit>
#include <climits>
#include <cstdint>

namespace sl::exec::detail {

template <typename T, typename TagT = std::uint8_t, std::uintptr_t TagWidth = 3ul>
    requires std::is_trivial_v<T*> && (TagWidth < sizeof(std::uintptr_t) * CHAR_BIT)
struct tagged_ptr {
    static constexpr std::uintptr_t tag_mask = bits::fill_ones<std::uintptr_t>(TagWidth);
    static constexpr std::uintptr_t ptr_mask = ~tag_mask;

private:
    constexpr explicit tagged_ptr(std::uintptr_t raw) : raw_{ raw } {}

public:
    constexpr static tagged_ptr make(T* ptr, TagT tag) {
        const auto ptr_part = std::bit_cast<std::uintptr_t>(ptr);
        const auto tag_part = static_cast<std::uintptr_t>(tag);
        ASSERT((ptr_part & tag_mask) == 0, "has to be aligned to TagWidth bytes", ptr_part, tag_mask);
        return tagged_ptr{ std::uintptr_t{ (ptr_part & ptr_mask) | (tag_part & tag_mask) } };
    }

    constexpr static tagged_ptr restore(std::uintptr_t raw) { return tagged_ptr{ raw }; }

    constexpr T* get_ptr() const { return std::bit_cast<T*>(raw_ & ptr_mask); }
    constexpr TagT get_tag() const { return static_cast<TagT>(raw_ & tag_mask); }
    constexpr std::uintptr_t get_raw() const { return raw_; }

private:
    std::uintptr_t raw_;
};

} // namespace sl::exec::detail
