//
// Created by usatiynyan.
//

#include "sl/exec/executor/inline.hpp"
#include <libassert/assert.hpp>

namespace sl::exec {

void inline_executor::schedule(task_node* task_node) noexcept {
    if (ASSUME_VAL(task_node != nullptr)) {
        task_node->execute();
    }
}

} // namespace sl::exec
