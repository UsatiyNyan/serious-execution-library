//
// Created by usatiynyan.
//

#pragma once

#include <sl/meta/lifetime/immovable.hpp>
#include <sl/meta/monad/maybe.hpp>
#include <sl/meta/monad/result.hpp>

#include <libassert/assert.hpp>

#include <coroutine>
#include <exception>
#include <iterator>

namespace sl::exec {

template <typename T>
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
        return std::move(handle_.promise()).get_yield();
    }

    [[nodiscard]] auto next_or_throw() {
        handle_.promise().resume_impl(handle_);
        return std::move(handle_.promise()).get_yield_or_throw();
    }

    [[nodiscard]] auto begin() { return iterator{ *this }; }
    [[nodiscard]] auto end() { return std::default_sentinel; }

private:
    handle_type handle_;
};

template <typename T>
struct generator<T>::promise_type {
    using yield_type = meta::result<T, std::exception_ptr>;

    // vvv compiler hooks
    auto get_return_object() { return generator{ handle_type::from_promise(*this) }; };

    auto initial_suspend() { return std::suspend_always{}; }
    auto final_suspend() noexcept { return std::suspend_always{}; }

    void unhandled_exception() { maybe_yield_.emplace(tl::unexpect, std::current_exception()); }

    template <std::convertible_to<T> From>
    auto yield_value(From&& from) {
        maybe_yield_.emplace(tl::in_place, std::forward<From>(from));
        return std::suspend_always{};
    }
    void return_void() {}
    // ^^^ compiler hooks

    void resume_impl(handle_type handle) {
        ASSUME(!maybe_yield_.has_value());
        handle.resume();
        ASSUME(maybe_yield_.has_value() || handle.done());
    }

    [[nodiscard]] meta::maybe<yield_type> get_yield() && noexcept {
        meta::maybe<yield_type> extracted;
        maybe_yield_.swap(extracted);
        return std::move(extracted);
    }
    [[nodiscard]] meta::maybe<T> get_yield_or_throw() && {
        return std::move(*this).get_yield().map([](yield_type yield_value) {
            if (!yield_value.has_value()) [[unlikely]] {
                std::rethrow_exception(yield_value.error());
            }
            return std::move(yield_value).value();
        });
    }

private:
    meta::maybe<yield_type> maybe_yield_;
};

template <typename T>
struct generator<T>::iterator {
    explicit iterator(generator<T>& self) : self_{ &self } { advance(); }

    iterator& operator++() {
        advance();
        return *this;
    }

    iterator operator++(int) {
        iterator tmp = *this;
        advance();
        return tmp;
    }

    [[nodiscard]] T& operator*() {
        ASSERT(maybe_value_.has_value());
        return maybe_value_.value();
    }

    [[nodiscard]] bool operator==(std::default_sentinel_t) const { return self_ == nullptr; }

private:
    void advance() {
        ASSERT(self_ != nullptr);
        maybe_value_ = self_->next_or_throw();
        if (!maybe_value_.has_value()) {
            self_ = nullptr; // signify end
        }
    }

private:
    meta::maybe<T> maybe_value_{};
    generator<T>* self_;
};

template <typename T>
generator<std::pair<std::size_t, T>> enumerate(generator<T> a_generator) {
    std::size_t index = 0;
    while (auto maybe_value = a_generator.next_or_throw()) {
        co_yield std::make_pair(index, std::move(maybe_value).value());
        ++index;
    }
};

} // namespace sl::exec
