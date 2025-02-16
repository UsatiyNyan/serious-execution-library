//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/coro/detail.hpp"

#include <sl/meta/lifetime/immovable.hpp>

#include <coroutine>
#include <iterator>

namespace sl::exec {

template <typename YieldT, typename ReturnT = void>
struct [[nodiscard]] generator : meta::immovable {
    // vvv compiler hooks
    struct promise_type;
    using handle_type = std::coroutine_handle<promise_type>;
    // ^^^ compiler hooks

    struct iterator;

private:
    explicit generator(handle_type handle) : handle_{ std::move(handle) } {}

public:
    ~generator() {
        if (handle_) {
            handle_.destroy();
        }
    }

    generator(generator&& other) noexcept : handle_{ std::exchange(other.handle_, {}) } {}

    [[nodiscard]] auto next() {
        handle_.promise().resume_impl(handle_);
        return handle_.promise().get_yield();
    }
    [[nodiscard]] auto next_or_throw() {
        handle_.promise().resume_impl(handle_);
        return handle_.promise().get_yield_or_throw();
    }
    [[nodiscard]] auto result() && {
        ASSERT(handle_.done());
        return std::move(handle_.promise()).get_return();
    }
    [[nodiscard]] auto result_or_throw() && {
        ASSERT(handle_.done());
        return std::move(handle_.promise()).get_return_or_throw();
    }

    auto begin() { return iterator{ *this }; }
    auto end() { return std::default_sentinel; }

private:
    handle_type handle_;
};

template <typename YieldT, typename ReturnT>
struct generator<YieldT, ReturnT>::promise_type : detail::promise_result_mixin<ReturnT> {
    // vvv compiler hooks
    auto get_return_object() { return generator{ handle_type::from_promise(*this) }; };

    auto initial_suspend() { return std::suspend_always{}; }
    auto final_suspend() noexcept { return std::suspend_always{}; }

    template <std::convertible_to<YieldT> From>
    auto yield_value(From&& from) {
        maybe_yield_.emplace(std::forward<From>(from));
        return std::suspend_always{};
    }
    // ^^^ compiler hooks

    void resume_impl(handle_type handle) {
        DEBUG_ASSERT(!maybe_yield_.has_value());
        handle.resume();
        DEBUG_ASSERT(maybe_yield_.has_value() || handle.done());
    }

    [[nodiscard]] meta::maybe<YieldT> get_yield() & noexcept {
        meta::maybe<YieldT> extracted{ std::move(maybe_yield_) };
        maybe_yield_.reset();
        return extracted;
    }
    [[nodiscard]] meta::maybe<YieldT> get_yield_or_throw() & {
        detail::promise_result_mixin<ReturnT>::try_rethrow();
        return get_yield();
    }

private:
    meta::maybe<YieldT> maybe_yield_;
};

template <typename YieldT, typename ReturnT>
struct [[nodiscard]] generator<YieldT, ReturnT>::iterator {
    using iterator_category = std::input_iterator_tag;
    using value_type = YieldT;
    using difference_type = std::ptrdiff_t;

    iterator() = default;
    explicit iterator(generator<YieldT, ReturnT>& self) : self_{ &self } { advance(); }

    iterator& operator++() {
        advance();
        return *this;
    }
    void operator++(int) { advance(); }

    [[nodiscard]] value_type& operator*() const {
        ASSERT(maybe_value_.has_value());
        return maybe_value_.value();
    }

    [[nodiscard]] value_type* operator->() const {
        ASSERT(maybe_value_.has_value());
        return &maybe_value_.value();
    }

    [[nodiscard]] bool operator==(std::default_sentinel_t) const { return self_ == nullptr; }

private:
    void advance() {
        ASSUME(self_ != nullptr);
        maybe_value_ = self_->next_or_throw();
        if (!maybe_value_.has_value()) {
            self_ = nullptr; // signify end
        }
    }

private:
    mutable meta::maybe<YieldT> maybe_value_{};
    generator<YieldT, ReturnT>* self_ = nullptr;
};

} // namespace sl::exec
