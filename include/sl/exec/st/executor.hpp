//
// Created by usatiynyan.
//
// single-threaded-executor may work similar to manual executor by calling execute_once();
//
// example of "endless" execution, while tasks are coming:
// ```cpp
// while (st_executor.execute_batch() > 0) {
//     // spin
// }
// ```
// or
// ```cpp
// st_executor.execute_until(...);
// ```
//

#pragma once

#include "sl/exec/generic.hpp"

namespace sl::exec {

class st_executor : public generic_executor {
public:
    ~st_executor() noexcept override;
    void schedule(generic_task_node* task_node) noexcept override;
    void stop() noexcept override { is_stopped_ = true; }

    // execute finite batch of currently scheduled tasks
    std::size_t execute_batch() noexcept;

    std::size_t execute_at_most(std::size_t n) noexcept;
    std::size_t execute_once() noexcept { return execute_at_most(1); }

    template <typename P>
    void execute_until(P&& predicate) noexcept {
        do {
            if (!predicate()) {
                stop();
            }
        } while (execute_batch() > 0);
    }

private:
    generic_task_list queue_;
    bool is_stopped_ = false;
};

} // namespace sl::exec
