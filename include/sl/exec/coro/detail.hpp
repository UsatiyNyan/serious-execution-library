//
// Created by usatiynyan.
//

#pragma once

#include <sl/meta/monad/maybe.hpp>
#include <sl/meta/monad/result.hpp>

#include <libassert/assert.hpp>

#include <exception>

namespace sl::exec::detail {

template <typename T>
struct promise_result_mixin_base {
    using return_type = meta::result<T, std::exception_ptr>;

    // vvv compiler hooks
    template <std::convertible_to<T> From>
    void return_value(From&& from) {
        maybe_return_.emplace(tl::in_place, std::forward<From>(from));
    }
    // ^^^ compiler hooks

protected:
    meta::maybe<return_type> maybe_return_;
};

template <>
struct promise_result_mixin_base<void> {
    using return_type = meta::result<void, std::exception_ptr>;

    // vvv compiler hooks
    void return_void() { maybe_return_.emplace(); }
    // ^^^ compiler hooks

protected:
    meta::maybe<return_type> maybe_return_;
};

template <typename T>
struct promise_result_mixin : public promise_result_mixin_base<T> {
    using base = promise_result_mixin_base<T>;
    using return_type = typename base::return_type;

    // vvv compiler hooks
    void unhandled_exception() { base::maybe_return_.emplace(tl::unexpect, std::current_exception()); }
    // ^^^ compiler hooks

    [[nodiscard]] return_type get_return() && noexcept {
        ASSUME(base::maybe_return_.has_value());
        return_type extracted{ std::move(base::maybe_return_).value() };
        base::maybe_return_.reset();
        return extracted;
    }

    [[nodiscard]] T get_return_or_throw() && {
        return_type return_value = std::move(*this).get_return();
        if (!return_value.has_value()) [[unlikely]] {
            std::rethrow_exception(return_value.error());
        }
        if constexpr (std::is_same_v<T, void>) {
            return;
        } else {
            return std::move(return_value).value();
        }
    }

    void try_rethrow() & {
        base::maybe_return_.map([](return_type& return_value) {
            if (!return_value.has_value()) [[unlikely]] {
                std::rethrow_exception(return_value.error());
            }
        });
    }
};

} // namespace sl::exec::detail
