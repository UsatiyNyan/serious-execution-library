//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/st/future.hpp"
#include "sl/io/socket.hpp"

namespace sl::eq {

class async_connection {
public:
    class view;

    using result_type = tl::expected<std::uint32_t, std::error_code>;
    using promise_type = exec::st::promise<result_type>;
    using future_type = exec::st::future<result_type>;

public:
    explicit async_connection(io::socket::connection connection) : connection_{ std::move(connection) } {}

    void handle_error();
    void handle_close();
    void handle_read();
    void handle_write();

    [[nodiscard]] const auto& socket() const { return connection_.socket; }

private:
    void handle_error_impl(std::error_code ec);

private:
    io::socket::connection connection_;
    tl::optional<std::tuple<std::span<std::byte>, promise_type>> read_state_{};
    tl::optional<std::tuple<std::span<const std::byte>, promise_type>> write_state_{};
};

class async_connection::view {
public:
    explicit view(async_connection& async_connection) : ref_{ async_connection } {}

    [[nodiscard]] future_type read(std::span<std::byte> buffer);
    [[nodiscard]] future_type write(std::span<const std::byte> buffer);

private:
    async_connection& ref_;
};

} // namespace sl::eq
