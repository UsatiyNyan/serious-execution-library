//
// Created by usatiynyan.
//

#pragma once

#include <sl/meta/traits/unique.hpp>

#include <cstdint>

namespace sl::exec {

struct cancel_handle {
    virtual ~cancel_handle() = default;

    virtual void try_cancel() & = 0;
};

inline cancel_handle& dummy_cancel_handle() {
    struct impl final : cancel_handle {
        void try_cancel() & override {}
    };

    static impl a_cancel_handle;
    return a_cancel_handle;
}

struct connection : meta::immovable {
    virtual ~connection() = default;

    virtual cancel_handle& emit() && = 0;
};

struct dummy_connection final : connection {
    cancel_handle& emit() && override { return dummy_cancel_handle(); }
};

struct ordered_connection : connection {
    [[nodiscard]] virtual std::uintptr_t get_ordering() const& = 0;
};

} // namespace sl::exec
