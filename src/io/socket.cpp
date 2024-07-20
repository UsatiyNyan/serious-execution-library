//
// Created by usatiynyan.
//

#include "sl/io/socket.hpp"

#include "sl/io/detail.hpp"

#include <arpa/inet.h>
#include <fcntl.h>
#include <libassert/assert.hpp>

namespace sl::io {

tl::expected<socket, std::error_code> socket::create(std::int32_t domain, std::int32_t type, std::int32_t protocol) {
    const int sfd = ::socket(domain, type, protocol);
    if (sfd == -1) {
        return tl::make_unexpected(detail::make_error_code_from_errno());
    }
    return socket{ .handle{ file{ sfd } } };
}

tl::expected<tl::monostate, std::error_code> socket::set_opt(std::int32_t level, std::int32_t name, std::int32_t opt) {
    const int result = ::setsockopt(handle.internal(), level, name, &opt, sizeof(opt));
    if (result == -1) {
        return tl::make_unexpected(detail::make_error_code_from_errno());
    }
    return tl::monostate{};
}

tl::expected<std::int32_t, std::error_code> socket::get_opt(std::int32_t level, std::int32_t name) {
    std::int32_t opt = 0;
    ::socklen_t optlen = sizeof(opt);
    const int result = ::getsockopt(handle.internal(), level, name, &opt, &optlen);
    ASSERT(optlen == sizeof(opt));
    if (result == -1) {
        return tl::make_unexpected(detail::make_error_code_from_errno());
    }
    return opt;
}

tl::expected<tl::monostate, std::error_code> socket::set_blocking(bool is_blocking) {
    return handle //
        .fcntl(F_GETFL, 0)
        .and_then([this, is_blocking](std::int32_t flags) {
            flags = is_blocking ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK);
            return handle.fcntl(F_SETFL, flags);
        })
        .map([](auto) { return tl::monostate{}; });
}

tl::expected<bool, std::error_code> socket::get_blocking() {
    return handle //
        .fcntl(F_GETFL, 0)
        .map([](std::int32_t flags) { return !(flags & O_NONBLOCK); });
}

tl::expected<socket::bound_server, std::error_code>
    socket::bind(std::uint16_t family, std::uint16_t port, std::uint32_t in_addr) && {
    // TODO: different sockaddr-s?
    const ::sockaddr_in address{
        .sin_family = family,
        .sin_port = ::htons(port),
        .sin_addr{ .s_addr = in_addr },
        .sin_zero{},
    };
    const int result = ::bind(handle.internal(), reinterpret_cast<const ::sockaddr*>(&address), sizeof(address));
    if (result == -1) {
        return tl::make_unexpected(detail::make_error_code_from_errno());
    }
    return socket::bound_server{ .socket = std::move(*this) };
}

tl::expected<socket::listening_server, std::error_code> socket::bound_server::listen(std::uint16_t backlog) && {
    ASSERT(backlog != 0);
    const int result = ::listen(socket.handle.internal(), static_cast<int>(backlog));
    if (result == -1) {
        return tl::make_unexpected(detail::make_error_code_from_errno());
    }
    return socket::listening_server{ .socket = std::move(socket) };
}

tl::expected<socket::connection, std::error_code> socket::listening_server::accept() {
    // TODO: different sockaddr-s?
    ::sockaddr_in address{};
    ::socklen_t addrlen = sizeof(address);
    const int cfd = ::accept(socket.handle.internal(), reinterpret_cast<::sockaddr*>(&address), &addrlen);
    if (cfd == -1) {
        return tl::make_unexpected(detail::make_error_code_from_errno());
    }
    ASSERT(addrlen == sizeof(address));
    return socket::connection{
        .socket{ .handle = file{ cfd } },
        .address = address,
    };
}

} // namespace sl::io
