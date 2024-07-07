//
// Created by usatiynyan.
// Mostly hoping for devirtualization of executors when concrete ones are used.
//

#pragma once

#include <type_traits>
#include <utility>

namespace sl::exec {

struct generic_task_node;

struct generic_executor {
    virtual ~generic_executor() noexcept = default;
    virtual bool schedule(generic_task_node* task_node) noexcept = 0;
    virtual void stop() noexcept = 0;
};

namespace detail {

// would not compile if there's no suitable specification
template <typename T>
struct schedule {
    template <typename TV>
    static bool impl(generic_executor&, TV&&) noexcept;
};

} // namespace detail

template <typename TV>
bool schedule(generic_executor& executor, TV&& value) {
    using T = std::decay_t<TV>;
    return detail::schedule<T>::impl(executor, std::forward<TV>(value));
}

} // namespace sl::exec
