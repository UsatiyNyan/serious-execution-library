//
// Created by usatiynyan.
//
// example of "endless" execution, while tasks are coming:
// ```cpp
// while (manual_executor.execute_batch() > 0) {
//     // spin
// }
// ```
//

#pragma once

#include "sl/exec/model/executor.hpp"

namespace sl::exec {

struct manual_executor final : executor {
    ~manual_executor() noexcept override;

    void schedule(task_node* task_node) noexcept override;
    void stop() noexcept override;

    // execute finite batch of currently scheduled tasks
    std::size_t execute_batch() noexcept;

    // less optimal then execute_batch
    std::size_t execute_at_most(std::size_t n) noexcept;

private:
    task_list task_queue_;
};

} // namespace sl::exec
