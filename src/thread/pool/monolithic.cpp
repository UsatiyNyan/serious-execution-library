//
// Created by usatiynyan.
//

#include "sl/exec/thread/pool/monolithic.hpp"

namespace sl::exec {

monolithic_thread_pool::monolithic_thread_pool(thread_pool_config config) {
    ASSERT(config.tcount > 0);
    workers_.reserve(config.tcount);
    for (std::uint32_t i = 0; i < config.tcount; ++i) {
        workers_.emplace_back([this] { worker_job(); });
    }
}

monolithic_thread_pool::~monolithic_thread_pool() noexcept {
    if (!workers_.empty()) {
        stop();
    }
}

void monolithic_thread_pool::schedule(task_node* task_node) noexcept {
    wg_.add(1u);
    tq_.push(task_node);
}

void monolithic_thread_pool::stop() noexcept {
    tq_.close();
    for (std::thread& worker : workers_) {
        if (ASSUME_VAL(worker.joinable())) {
            worker.join();
        }
    }
    workers_.clear();
}

void monolithic_thread_pool::wait_idle() { wg_.wait(); }

void monolithic_thread_pool::worker_job() {
    while (auto* maybe_task = tq_.try_pop()) {
        auto* task = maybe_task->downcast();
        task->execute();
        wg_.done();
    }
}

} // namespace sl::exec
