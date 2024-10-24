//
// Created by usatiynyan.
// This one is for manual implementations, where everything is inlined or controlled from main thread.
//

#pragma once

#include <libassert/assert.hpp>

namespace sl::exec {

struct nowait_event {
    void set() { is_set_ = true; }

    void wait() { ASSERT(is_set_); }

private:
    bool is_set_ = false;
};

} // namespace sl::exec
