//
// Created by usatiynyan.
// "Unsynchronized" future, a building block for single-threaded coroutines.
// TODO: 2 allocations:
// - for shared_state
// - for functor
// is there a better way?
//

#pragma once

#include "sl/exec/generic/async.hpp"
#include "sl/exec/generic/executor.hpp"
#include "sl/exec/generic/functor.hpp"

#include <coroutine>
#include <function2/function2.hpp>
#include <sl/meta/lifetime/immovable.hpp>
#include <tl/expected.hpp>
#include <tl/optional.hpp>

namespace sl::exec::st {
namespace detail {

template <typename T>
struct shared_state {
    tl::optional<T> maybe_value{};
    generic_task_node* maybe_callback = nullptr;
    generic_executor* maybe_executor = nullptr;
};

} // namespace detail

template <typename T>
class [[nodiscard]] promise : meta::immovable {
public:
    explicit promise(detail::shared_state<T>* state) : state_{ state } {}
    promise(promise&& other) noexcept : state_{ std::exchange(other.state_, nullptr) } {}
    ~promise() noexcept { ASSERT(state_ == nullptr, "promise was not fulfilled"); }

    template <typename... Args>
    void set_value(Args&&... args) && {
        detail::shared_state<T>* state = std::exchange(state_, nullptr);
        ASSERT(state);

        ASSERT(!state->maybe_value.has_value());
        state->maybe_value.emplace(std::forward<Args>(args)...);
        ASSERT(state->maybe_value.has_value());

        if (state->maybe_callback != nullptr) {
            ASSERT(state->maybe_executor != nullptr);
            state->maybe_executor->schedule(state->maybe_callback);
        }
    }

private:
    detail::shared_state<T>* state_;
};

template <typename T>
class [[nodiscard]] future : meta::immovable {
public:
    // vvv compiler hooks
    class awaiter;
    [[nodiscard]] auto operator co_await() && { return future<T>::awaiter{ std::move(*this) }; }
    // ^^^ compiler hooks

    explicit future(detail::shared_state<T>* state) : state_{ state } {}
    future(future&& other) noexcept : state_{ std::exchange(other.state_, nullptr) } {}
    ~future() noexcept { ASSERT(state_ == nullptr, "future was not awaited"); }

    template <typename FV>
        requires std::is_nothrow_invocable_r_v<void, FV, T>
    void set_callback(generic_executor& executor, FV&& f) && {
        detail::shared_state<T>* state = std::exchange(state_, nullptr);
        ASSERT(state);

        ASSERT(state->maybe_callback == nullptr && state->maybe_executor == nullptr);
        state->maybe_executor = &executor;
        state->maybe_callback = allocate_functor_task_node([f = std::forward<FV>(f), state] mutable noexcept {
            ASSERT(state->maybe_value.has_value());
            std::move(f)(std::move(state->maybe_value).value());
            delete state;
        });
        ASSERT(state->maybe_callback != nullptr && state->maybe_executor != nullptr);

        if (state->maybe_value.has_value()) {
            state->maybe_executor->schedule(state->maybe_callback);
        }
    }

private:
    detail::shared_state<T>* state_;
};

template <typename T>
class future<T>::awaiter {
public:
    explicit awaiter(future&& a_future) : future_{ std::move(a_future) } {}

    // vvv compiler hooks
    bool await_ready() noexcept { return false; }
    template <typename U>
    void await_suspend(std::coroutine_handle<async_promise<U>> caller) noexcept {
        auto& caller_promise = caller.promise();
        ASSERT(caller_promise.executor != nullptr);
        std::move(future_).set_callback(*caller_promise.executor, [this, caller](T&& value) noexcept {
            value_.emplace(std::move(value));
            caller.resume();
        });
    }
    [[nodiscard]] auto await_resume() noexcept {
        ASSERT(value_.has_value());
        return std::move(value_).value();
    }
    // ^^^ compiler hooks

private:
    future future_;
    tl::optional<T> value_;
};

template <typename T>
std::tuple<future<T>, promise<T>> make_contract() {
    auto* shared_state = new detail::shared_state<T>;
    return std::make_tuple(future<T>{ shared_state }, promise<T>{ shared_state });
}

} // namespace sl::exec::st
