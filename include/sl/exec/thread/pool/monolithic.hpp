//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/model/executor.hpp"

#include "sl/exec/thread/detail/unbound_blocking_queue.hpp"
#include "sl/exec/thread/pool/config.hpp"
#include "sl/exec/thread/sync/wait_group.hpp"

#include <sl/meta/lifetime/immovable.hpp>

#include <thread>

namespace sl::exec {

struct monolithic_thread_pool final
    : executor
    , meta::immovable {
    // starts when initialized
    explicit monolithic_thread_pool(thread_pool_config config);
    ~monolithic_thread_pool() noexcept override;

    void schedule(task_node* task_node) noexcept override;
    void stop() noexcept override;

    void wait_idle();

private:
    void worker_job();

private:
    std::vector<std::thread> workers_;
    detail::unbound_blocking_queue<task_node> tq_;
    wait_group wg_;
};

} // namespace sl::exec
