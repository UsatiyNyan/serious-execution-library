//
// Created by usatiynyan.
//

#pragma once

#include <coroutine>
#include <exception>
#include <iterator>
#include <libassert/assert.hpp>
#include <tl/expected.hpp>
#include <tl/optional.hpp>

namespace sl::coro {

template <typename T>
class generator {
public:
    // vvv compiler hooks
    class promise_type;
    using handle_type = std::coroutine_handle<promise_type>;
    // ^^^ compiler hooks

    class iterator;

private:
    explicit generator(handle_type handle) : handle_{ std::move(handle) } {}

public:
    ~generator() {
        if (handle_) {
            handle_.destroy();
        }
    }

    generator(const generator&) = delete;
    generator& operator=(const generator&) = delete;
    generator(generator&& other) noexcept : handle_{ std::exchange(other.handle_, {}) } {}
    generator& operator=(generator&& other) noexcept { std::swap(handle_, other.handle_); }

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
class generator<T>::promise_type {
public:
    using yield_type = tl::expected<T, std::exception_ptr>;

    // vvv compiler hooks
    auto get_return_object() { return generator{ handle_type::from_promise(*this) }; };

    auto initial_suspend() { return std::suspend_always{}; }
    auto final_suspend() noexcept { return std::suspend_always{}; }

    void unhandled_exception() { maybe_yield_.emplace(tl::make_unexpected(std::current_exception())); }

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

    [[nodiscard]] tl::optional<yield_type> get_yield() && noexcept {
        tl::optional<yield_type> extracted;
        maybe_yield_.swap(extracted);
        return std::move(extracted);
    }
    [[nodiscard]] tl::optional<T> get_yield_or_throw() && {
        return std::move(*this).get_yield().map([](yield_type yield_value) {
            if (!yield_value.has_value()) [[unlikely]] {
                std::rethrow_exception(yield_value.error());
            }
            return std::move(yield_value).value();
        });
    }

private:
    tl::optional<yield_type> maybe_yield_;
};

template <typename T>
class generator<T>::iterator {
public:
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
    generator<T>* self_;
    tl::optional<T> maybe_value_;
};

template <typename T>
generator<std::pair<std::size_t, T>> enumerate(generator<T> a_generator) {
    std::size_t index = 0;
    while (auto maybe_value = a_generator.next_or_throw()) {
        co_yield std::make_pair(index, std::move(maybe_value).value());
        ++index;
    }
};

} // namespace sl::coro
