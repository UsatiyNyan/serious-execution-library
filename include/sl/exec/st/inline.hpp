//
// Created by usatiynyan.
//

#include "sl/exec/generic/executor.hpp"
#include <libassert/assert.hpp>

namespace sl::exec::st {

class inline_executor : public generic_executor {
public:
    static inline_executor& instance() {
        static inline_executor instance;
        return instance;
    }

    void schedule(generic_task_node* task_node) noexcept override {
        if (ASSUME_VAL(task_node != nullptr) && !is_stopped_) [[likely]] {
            (void)task_node->execute();
        }
    }
    void stop() noexcept override { is_stopped_ = true; }

private:
    bool is_stopped_ = false;
};

} // namespace sl::exec::st
