//
// Created by usatiynyan.
//

#pragma once

#include <coroutine>
#include <exception>
#include <libassert/assert.hpp>
#include <tl/optional.hpp>

namespace sl::exec {
namespace detail {

template <typename T>
class task_promise_base {
public:
    // vvv compiler hooks
    template <std::convertible_to<T> From>
    void return_value(From&& from) {
        value_.emplace(std::forward<From>(from));
    }
    // ^^^ compiler hooks

protected:
    T release() && {
        ASSUME(value_.has_value());
        tl::optional<T> extracted;
        value_.swap(extracted);
        return std::move(extracted).value();
    }

private:
    tl::optional<T> value_;
};

template <>
class task_promise_base<void> {
public:
    // vvv compiler hooks
    void return_void() {}
    // ^^^ compiler hooks

protected:
    void release() && {}
};

} // namespace detail

template <typename T>
class task {
public:
    // vvv compiler hooks
    class promise_type;
    using handle_type = std::coroutine_handle<promise_type>;

    bool await_ready() { return false; }
    auto await_suspend(std::coroutine_handle<> caller) {
        // ASSUME(!handle_.done());
        // handle_.promise().continuation = caller;
        return caller;
    }
    [[nodiscard]] T await_resume() {
        ASSUME(handle_.done());
        // changing to the rvalue intentionally
        return std::move(*this).result();
    }
    // ^^^ compiler hooks

    explicit task(handle_type handle) : handle_{ handle } {}
    ~task() { handle_.destroy(); }

    auto result() && { return std::move(handle_.promise()).release(); }

private:
    handle_type handle_;
};

template <typename T>
class task<T>::promise_type : public detail::task_promise_base<T> {
public:
    // vvv compiler hooks
    auto get_return_object() { return task{ handle_type::from_promise(*this) }; };

    auto initial_suspend() { return std::suspend_never{}; }
    auto final_suspend() noexcept {
        struct continue_caller {
            bool await_ready() noexcept { return false; }
            void await_suspend(handle_type handle) noexcept {
                if (auto c = handle.promise().continuation) {
                    // never comes in here if tasks are eagerly executed!!!
                    c.resume();
                }
            }
            void await_resume() noexcept {}
        };
        return continue_caller{};
    }

    void unhandled_exception() { exception_ = std::current_exception(); }

    // either return_void or return_value are mixed in from the base
    // ^^^ compiler hooks

    auto release() && {
        if (exception_) [[unlikely]] {
            std::rethrow_exception(exception_);
        }
        return std::move(*this).detail::template task_promise_base<T>::release();
    }

public:
    std::coroutine_handle<> continuation{};

private:
    std::exception_ptr exception_{};
};

} // namespace sl::exec
