//
// Created by usatiynyan.
// TODO: return awaitable
//

#include "sl/exec/generic/async.hpp"
#include "sl/exec/generic/executor.hpp"
#include "sl/exec/generic/functor.hpp"
#include "sl/exec/generic/on.hpp"

#include <type_traits>
#include <utility>

namespace sl::exec {
namespace detail {

template <typename T>
struct schedule {
    template <typename TV>
    static void impl(generic_executor&, TV&&) noexcept; // would not compile if there's no suitable specification
};

template <FunctorTaskNodeRequirement F>
struct schedule<F> {
    template <typename FV>
    static void impl(generic_executor& executor, FV&& f) {
        auto* node = functor_task_node<F>::allocate(std::forward<FV>(f));
        ASSERT(node != nullptr);
        executor.schedule(node);
    }
};

template <>
struct schedule<async<void>> {
    static void impl(generic_executor& executor, async<void> coro) {
        constexpr auto trampoline = [](generic_executor& executor, async<void> coro) noexcept -> async<void> {
            co_await on(executor);
            co_await std::move(coro);
        };
        trampoline(executor, std::move(coro)).release().resume();
    }
};

} // namespace detail

template <typename TV>
void schedule(generic_executor& executor, TV&& value) {
    using T = std::decay_t<TV>;
    detail::schedule<T>::impl(executor, std::forward<TV>(value));
}

} // namespace sl::exec
