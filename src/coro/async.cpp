//
// Created by usatiynyan.
//

#include "sl/exec/coro/async.hpp"

namespace sl::exec {

void coro_schedule(executor& executor, async<void> coro) {
    // TODO(@usatiynyan): check for memory leaks
    auto handle = std::move(coro).release();
    auto& promise = handle.promise();
    executor.schedule(&promise);
}

} // namespace sl::exec
