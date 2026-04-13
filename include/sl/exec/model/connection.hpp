//
// Created by usatiynyan.
//

#pragma once

#include <limits>
#include <sl/meta/traits/unique.hpp>

#include <cstdint>
#include <utility>

namespace sl::exec {

template <typename CancelHandleT>
concept CancelHandle = requires(CancelHandleT&& cancel_handle) {
    { std::move(cancel_handle).try_cancel() } noexcept -> std::same_as<void>;
};

template <typename ConnectionT>
concept Connection = requires(ConnectionT&& connection) {
    { std::move(connection).emit() } noexcept -> CancelHandle;
};

template <typename OrderedT>
concept Ordered = requires(const OrderedT& ordered) {
    { ordered.get_ordering() } noexcept -> std::same_as<std::uintptr_t>;
};

template <typename ConnectionT>
struct proxy_cancel_handle final {
    constexpr explicit proxy_cancel_handle(ConnectionT* connection) : connection_{ connection } {}

    constexpr void try_cancel() && noexcept { std::move(*connection_).try_cancel(); }

private:
    ConnectionT* connection_;
};

struct dummy_cancel_handle final {
    constexpr void try_cancel() && noexcept {}
};

struct dummy_connection final {
    constexpr CancelHandle auto emit() && noexcept { return dummy_cancel_handle{}; }
};

template <Connection ConnectionT>
constexpr std::uintptr_t get_connection_ordering(const ConnectionT& connection) {
    if constexpr (Ordered<ConnectionT>) {
        return connection.get_ordering();
    } else {
        return std::numeric_limits<std::uintptr_t>::max();
    }
}

} // namespace sl::exec
