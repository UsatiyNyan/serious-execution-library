//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/model/executor.hpp"

#include "sl/exec/thread/detail/atomic.hpp"
#include "sl/exec/thread/detail/unbound_blocking_queue.hpp"
#include "sl/exec/thread/pool/config.hpp"
#include "sl/exec/thread/sync/wait_group.hpp"

#include <sl/meta/assert.hpp>
#include <sl/meta/traits/unique.hpp>

#include <thread>

namespace sl::exec {

template <template <typename> typename Atomic = detail::atomic>
struct monolithic_thread_pool final
    : executor
    , meta::immovable {

    // starts when initialized
    explicit monolithic_thread_pool(thread_pool_config config) {
        ASSERT(config.tcount > 0);
        workers_.reserve(config.tcount);
        for (std::uint32_t i = 0; i < config.tcount; ++i) {
            workers_.emplace_back([this] { worker_job(); });
        }
    }
    ~monolithic_thread_pool() noexcept override {
        if (!workers_.empty()) {
            stop();
        }
    }

    void schedule(task_node* task_node) noexcept override {
        wg_.add(1u);
        tq_.push(task_node);
    }

    void stop() noexcept override {
        tq_.close();
        for (std::thread& worker : workers_) {
            if (ASSERT_VAL(worker.joinable())) {
                worker.join();
            }
        }
        workers_.clear();
    }


    void wait_idle() { wg_.wait(); }

private:
    void worker_job() {
        while (auto* maybe_task = tq_.try_pop()) {
            auto* task = maybe_task->downcast();
            task->execute();
            wg_.done();
        }
    }

private:
    std::vector<std::thread> workers_;
    detail::unbound_blocking_queue<task_node> tq_;
    wait_group<Atomic> wg_;
};

} // namespace sl::exec
