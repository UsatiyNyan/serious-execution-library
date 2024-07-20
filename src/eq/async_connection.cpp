//
// Created by usatiynyan.
//

#include "sl/eq/async_connection.hpp"
#include "sl/exec/st/future.hpp"


namespace sl::eq {

void async_connection::handle_read() {
    if (!read_state_.has_value()) {
        return;
    }
    auto& [buffer, promise] = read_state_.value();
    auto read_result = connection_.handle.read(buffer);
    if (!read_result.has_value()) {
        const auto ec = read_result.error();
        if (ec == std::errc::resource_unavailable_try_again || ec == std::errc::operation_would_block) {
            // will retry
            return;
        }
    }
    std::move(promise).set_value(std::move(read_result));
    read_state_.reset();
}

void async_connection::handle_write() {
    if (!write_state_.has_value()) {
        return;
    }
    auto& [buffer, promise] = write_state_.value();
    auto write_result = connection_.handle.write(buffer);
    if (!write_result.has_value()) {
        const auto ec = write_result.error();
        if (ec == std::errc::resource_unavailable_try_again || ec == std::errc::operation_would_block) {
            // will retry
            return;
        }
    }
    std::move(promise).set_value(std::move(write_result));
    write_state_.reset();
}

async_connection::future_type async_connection::view::read(std::span<std::byte> buffer) {
    auto [future, promise] = exec::st::make_contract<tl::expected<std::uint32_t, std::error_code>>();
    ASSERT(!ref_.read_state_.has_value());
    ref_.read_state_.emplace(buffer, std::move(promise));
    ref_.handle_read();
    return std::move(future);
}

async_connection::future_type async_connection::view::write(std::span<const std::byte> buffer) {
    auto [future, promise] = exec::st::make_contract<tl::expected<std::uint32_t, std::error_code>>();
    ASSERT(!ref_.write_state_.has_value());
    ref_.write_state_.emplace(buffer, std::move(promise));
    ref_.handle_write();
    return std::move(future);
}

} // namespace sl::eq
