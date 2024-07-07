//
// Created by usatiynyan.
// Since we are already allocating tasks on heap its fine to have virtual interfaces.
//

#pragma once

#include "sl/exec/generic/executor.hpp"

#include <sl/meta/intrusive/forward_list.hpp>
#include <sl/meta/lifetime/defer.hpp>
#include <tl/optional.hpp>

namespace sl::exec {

struct generic_executor;

// default capacity is enough for 2 pointers (so basically this + method)
using generic_cleanup = meta::defer<fu2::capacity_default>;

struct generic_task {
    virtual ~generic_task() noexcept = default;
    [[nodiscard]] virtual generic_cleanup execute(generic_executor&) noexcept = 0;
    [[nodiscard]] virtual generic_cleanup cancel() noexcept = 0;
};

struct generic_task_node
    : generic_task
    , meta::intrusive_forward_list_node<generic_task_node> {

    generic_cleanup execute(generic_executor& an_executor) noexcept override {
        executor.emplace(an_executor);
        return execute();
    }

    // oh fucking god no to hell with OOP
    virtual generic_cleanup execute() noexcept = 0;

public:
    tl::optional<generic_executor&> executor;
};

using generic_task_list = meta::intrusive_forward_list<generic_task_node>;

} // namespace sl::exec
