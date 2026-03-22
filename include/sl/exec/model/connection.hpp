//
// Created by usatiynyan.
//

#pragma once

#include <sl/meta/traits/unique.hpp>

namespace sl::exec {

struct cancel_handle {
    virtual ~cancel_handle() = default;

    [[nodiscard]] virtual bool try_cancel() & = 0;
};

inline cancel_handle& dummy_cancel_handle() {
    struct impl final : cancel_handle {
        bool try_cancel() & override { return false; }
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

} // namespace sl::exec
