//
// Created by usatiynyan.
//

#include "sl/exec/algo.hpp"
#include "sl/exec/model.hpp"
#include "sl/exec/thread.hpp"

#include <gtest/gtest.h>
#include <sl/meta/func/undefined.hpp>

namespace sl::exec {

TEST(thread, monolithicThreadPool) {
    monolithic_thread_pool background_executor{ thread_pool_config::with_hw_limit(1u) };
    const tl::optional<meta::result<std::thread::id, meta::undefined>> maybe_result =
        schedule(
            background_executor,
            [] -> meta::result<std::thread::id, meta::undefined> { return std::this_thread::get_id(); }
        )
        | get<atomic_event>();
    ASSERT_NE(*maybe_result, std::this_thread::get_id());
}

} // namespace sl::exec
