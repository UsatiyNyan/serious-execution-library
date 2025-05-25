//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/model/executor.hpp"

#include <sl/meta/type/unit.hpp>

#include <libassert/assert.hpp>

namespace sl::exec {
namespace detail {

struct inline_executor final : executor {
    constexpr void schedule(task_node* task_node) noexcept override {
        if (ASSUME_VAL(task_node != nullptr)) {
            task_node->execute();
        }
    }
    constexpr void stop() noexcept override {}
};

} // namespace detail

inline executor& inline_executor() {
    static detail::inline_executor ie;
    return ie;
}

} // namespace sl::exec
