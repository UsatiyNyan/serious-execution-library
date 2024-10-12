//
// Created by usatiynyan.
// Since we are already allocating tasks on heap its fine to have virtual interfaces.
//

#pragma once

#include <sl/meta/intrusive/forward_list.hpp>

namespace sl::exec {

struct task {
    virtual ~task() noexcept = default;
    virtual void execute() noexcept = 0;
};

struct task_node
    : task
    , meta::intrusive_forward_list_node<task_node> {};

using task_list = meta::intrusive_forward_list<task_node>;

} // namespace sl::exec
