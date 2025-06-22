//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/thread/detail/atomic.hpp"
#include "sl/exec/thread/detail/polyfill.hpp"

#include <sl/meta/traits/unique.hpp>

#include <libassert/assert.hpp>

#include <array>
#include <utility>

namespace sl::exec {
namespace detail {

template <typename T, template <typename> typename Atomic>
struct [[nodiscard]] arc_storage : meta::immovable {
    template <typename... Args>
    explicit arc_storage(std::uint32_t refcount, Args&&... args)
        : value_{ std::forward<Args>(args)... }, refcount_{ refcount } {}

public:
    [[nodiscard]] std::uint32_t incref(std::uint32_t diff = 1) & {
        return refcount_.fetch_add(diff, std::memory_order::relaxed);
    }

    [[nodiscard]] std::uint32_t decref(std::uint32_t diff = 1) & {
        const std::uint32_t prev = refcount_.fetch_sub(diff, std::memory_order::relaxed);
        if (prev == diff) {
            delete this;
        }
        return prev;
    }

public:
    T& value() & { return value_; }
    const T& value() const& { return value_; }

private:
    T value_;
    alignas(hardware_destructive_interference_size) Atomic<std::uint32_t> refcount_;
};

} // namespace detail

template <typename T, template <typename> typename Atomic = detail::atomic>
struct [[nodiscard]] arc {
private:
    explicit arc(detail::arc_storage<T, Atomic>* storage) : storage_{ storage } {}

    template <std::uint32_t N, std::uint32_t... Is>
    static auto make_n_impl(detail::arc_storage<T, Atomic>* storage, std::integer_sequence<std::uint32_t, Is...>) {
        return std::array<arc, N>{ (arc{ storage }, Is)... };
    }

public:
    template <typename... Args>
    static arc make(Args&&... args) {
        return arc{ new detail::arc_storage<T, Atomic>{
            /* refcount = */ 1u,
            /* args = */ std::forward<Args>(args)...,
        } };
    }

    template <std::uint32_t N, typename... Args>
    static std::array<arc, N> make_n(Args&&... args) {
        auto* storage = new detail::arc_storage<T, Atomic>{
            /* refcount = */ N,
            /* args = */ std::forward<Args>(args)...,
        };

        return make_n_impl<N>(storage, std::make_integer_sequence<std::uint32_t, N>());
    }

public:
    arc(const arc& other) : storage_{ other.storage_ } {
        ASSERT(storage_);
        const std::uint32_t prev = storage_->incref();
        ASSERT(prev > 0u);
    }
    arc(arc&& other) : storage_{ std::exchange(other.storage_, nullptr) } { ASSERT(storage_); }

    arc& operator=(const arc&) = delete;
    arc& operator=(arc&&) = delete;

    ~arc() {
        if (nullptr != storage_) {
            const std::uint32_t prev = storage_->decref();
            ASSERT(prev > 0u);
        }
    }

public:
    T& value() & { return storage_->value(); }
    const T& value() const& { return storage_->value(); }

    T& operator*() & { return value(); }
    const T& operator*() const& { return value(); }

    T* operator->() & { return &value(); }
    const T* operator->() const& { return &value(); }

private:
    detail::arc_storage<T, Atomic>* storage_;
};

} // namespace sl::exec
