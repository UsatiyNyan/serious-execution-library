//
// Created by usatiynyan.
// Since we are already allocating tasks on heap its fine to have virtual interfaces.
//

#pragma once

#include <sl/meta/intrusive/forward_list.hpp>
#include <sl/meta/lifetime/defer.hpp>
#include <type_traits>

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
    , meta::intrusive_forward_list_node<generic_task_node> {};

using generic_task_list = meta::intrusive_forward_list<generic_task_node>;

template <typename F>
    requires(std::is_nothrow_invocable_r_v<void, F, generic_executor&>)
class functor_task_node : public generic_task_node {
    template <typename FV>
    explicit functor_task_node(FV&& f) : f_{ std::forward<FV>(f) } {}

public:
    template <typename FV>
    static functor_task_node* allocate(FV&& f) noexcept {
        return new (std::nothrow) functor_task_node{ std::forward<FV>(f) };
    }

    generic_cleanup execute(generic_executor& executor) noexcept override {
        f_(executor);
        return generic_cleanup{ [this] { delete this; } };
    }
    generic_cleanup cancel() noexcept override {
        return generic_cleanup{ [this] { delete this; } };
    }

private:
    F f_;
};

template <typename FV>
functor_task_node(FV&&) -> functor_task_node<std::decay_t<FV>>;

} // namespace sl::exec
