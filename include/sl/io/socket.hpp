//
// Created by usatiynyan.
//

#pragma once

#include "sl/io/file.hpp"

#include <netinet/in.h>
#include <tl/expected.hpp>

#include <system_error>

namespace sl::io {

struct socket {
    struct bound_server;
    struct listening_server;
    using server = listening_server;
    struct connection;

public:
    [[nodiscard]] static tl::expected<socket, std::error_code>
        create(std::int32_t domain, std::int32_t type, std::int32_t protocol);

    [[nodiscard]] tl::expected<tl::monostate, std::error_code>
        set_opt(std::int32_t level, std::int32_t name, std::int32_t opt);
    [[nodiscard]] tl::expected<std::int32_t, std::error_code> get_opt(std::int32_t level, std::int32_t name);

    [[nodiscard]] tl::expected<tl::monostate, std::error_code> set_blocking(bool is_blocking);
    [[nodiscard]] tl::expected<bool, std::error_code> get_blocking();

    [[nodiscard]] tl::expected<bound_server, std::error_code>
        bind(std::uint16_t family, std::uint16_t port, std::uint32_t in_addr) &&;

public:
    file handle;
};

struct socket::bound_server {
    // backlog should be more than 0 and it will be truncated if exceeds 4096 by system
    [[nodiscard]] tl::expected<listening_server, std::error_code> listen(std::uint16_t backlog) &&;

public:
    socket socket;
};

struct socket::listening_server {
    [[nodiscard]] tl::expected<socket::connection, std::error_code> accept();

public:
    socket socket;
};

struct socket::connection {
    socket socket;
    ::sockaddr_in address; // TODO: ipv6
};

} // namespace sl::io
