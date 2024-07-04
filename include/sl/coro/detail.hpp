//
// Created by usatiynyan.
//

#pragma once

#include <exception>
#include <libassert/assert.hpp>
#include <tl/expected.hpp>
#include <tl/optional.hpp>

namespace sl::coro::detail {

template <typename T>
class promise_result_mixin_base {
public:
    using return_type = tl::expected<T, std::exception_ptr>;

    // vvv compiler hooks
    template <std::convertible_to<T> From>
    void return_value(From&& from) {
        maybe_return_.emplace(tl::in_place, std::forward<From>(from));
    }
    // ^^^ compiler hooks

protected:
    tl::optional<return_type> maybe_return_;
};

template <>
class promise_result_mixin_base<void> {
public:
    using return_type = tl::expected<void, std::exception_ptr>;

    // vvv compiler hooks
    void return_void() { maybe_return_.emplace(); }
    // ^^^ compiler hooks

protected:
    tl::optional<return_type> maybe_return_;
};

template <typename T>
class promise_result_mixin : public promise_result_mixin_base<T> {
public:
    using base = promise_result_mixin_base<T>;
    using return_type = typename base::return_type;

    // vvv compiler hooks
    void unhandled_exception() { base::maybe_return_.emplace(tl::make_unexpected(std::current_exception())); }
    // ^^^ compiler hooks

    [[nodiscard]] return_type get_return() && noexcept {
        ASSUME(base::maybe_return_.has_value());
        tl::optional<return_type> extracted;
        base::maybe_return_.swap(extracted);
        return std::move(extracted).value();
    }

    [[nodiscard]] T get_return_or_throw() && {
        return_type return_value = std::move(*this).get_return();
        if (!return_value.has_value()) [[unlikely]] {
            std::rethrow_exception(return_value.error());
        }
        return std::move(return_value).value();
    }
};

} // namespace sl::exec::detail
