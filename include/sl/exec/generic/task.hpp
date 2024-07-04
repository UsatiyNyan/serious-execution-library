//
// Created by usatiynyan.
// Since we are already allocating tasks on heap its fine to have virtual interfaces.
//

#pragma once

#include <sl/meta/intrusive/forward_list.hpp>
#include <sl/meta/lifetime/defer.hpp>
#include <type_traits>

namespace sl::exec {

// default capacity is enough for 2 pointers (so basically this + method)
using generic_cleanup = meta::defer<fu2::capacity_default>;

struct generic_task {
    virtual ~generic_task() noexcept = default;
    virtual generic_cleanup execute() noexcept = 0;
    virtual generic_cleanup cancel() noexcept = 0;
};

struct generic_task_node
    : generic_task
    , meta::intrusive_forward_list_node<generic_task_node> {};

template <typename F>
    requires(std::is_nothrow_invocable_r_v<void, F>)
class functor_task_node : public generic_task_node {
    template <typename FV>
    explicit functor_task_node(FV&& f) : f_{ std::forward<FV>(f) } {}

    template <typename FV>
    friend functor_task_node* allocate_functor_task_node(FV&& f) noexcept;

public:
    generic_cleanup execute() noexcept override {
        f_();
        return [this] { delete this; };
    }
    generic_cleanup cancel() noexcept override {
        return [this] { delete this; };
    }

private:
    F f_;
};

template <typename FV>
functor_task_node(FV&&) -> functor_task_node<std::decay_t<FV>>;

template <typename FV>
auto* allocate_functor_task_node(FV&& f) noexcept {
    return new (std::nothrow) functor_task_node{ std::forward<FV>(f) };
}

} // namespace sl::exec
