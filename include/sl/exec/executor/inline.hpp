//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/executor/executor.hpp"

namespace sl::exec {

class inline_executor final : public executor {
public:
    static inline_executor& instance() {
        static inline_executor instance;
        return instance;
    }

    void schedule(task_node* task_node) noexcept override;
};

} // namespace sl::exec
