//
// Created by usatiynyan.
//

#include "sl/exec/coro/async.hpp"

namespace sl::exec {

void coro_schedule(executor& executor, async<void> coro) {
    auto handle = std::move(coro).release();
    auto& promise = handle.promise();
    promise.continuation = nullptr; // explicitly released, need to be destroyed in final awaiter
    executor.schedule(&promise);
}

} // namespace sl::exec
