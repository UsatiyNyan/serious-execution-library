//
// Created by usatiynyan.
//

#pragma once

#include <exception>
#include <libassert/assert.hpp>
#include <tl/expected.hpp>
#include <tl/optional.hpp>

namespace sl::exec::detail {

template <typename T>
class promise_result_mixin_base {
public:
    using result_type = tl::expected<T, std::exception_ptr>;

    // vvv compiler hooks
    template <std::convertible_to<T> From>
    void return_value(From&& from) {
        maybe_result_.emplace(tl::in_place, std::forward<From>(from));
    }
    // ^^^ compiler hooks

protected:
    tl::optional<result_type> maybe_result_;
};

template <>
class promise_result_mixin_base<void> {
public:
    using result_type = tl::expected<void, std::exception_ptr>;

    // vvv compiler hooks
    void return_void() { maybe_result_.emplace(); }
    // ^^^ compiler hooks

protected:
    tl::optional<result_type> maybe_result_;
};

template <typename T>
class promise_result_mixin : public promise_result_mixin_base<T> {
public:
    using base = promise_result_mixin_base<T>;
    using result_type = typename base::result_type;

    // vvv compiler hooks
    void unhandled_exception() { base::maybe_result_.emplace(tl::make_unexpected(std::current_exception())); }
    // ^^^ compiler hooks

    [[nodiscard]] result_type result() && noexcept {
        ASSUME(base::maybe_result_.has_value());
        tl::optional<result_type> extracted;
        base::maybe_result_.swap(extracted);
        return std::move(extracted).value();
    }

    [[nodiscard]] T value_or_throw() && {
        result_type result_value = std::move(*this).result();
        if (!result_value.has_value()) [[unlikely]] {
            std::rethrow_exception(result_value.error());
        }
        return std::move(result_value).value();
    }
};

} // namespace sl::exec::detail
