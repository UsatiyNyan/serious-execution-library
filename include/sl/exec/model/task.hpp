//
// Created by usatiynyan.
//

#pragma once

#include <sl/meta/intrusive/forward_list.hpp>

namespace sl::exec {

struct task {
    virtual ~task() noexcept = default;
    virtual void execute() noexcept = 0;
    virtual void cancel() noexcept = 0;
};

struct task_node
    : task
    , meta::intrusive_forward_list_node<task_node> {};

using task_list = meta::intrusive_forward_list<task_node>;

} // namespace sl::exec
