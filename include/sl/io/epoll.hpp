//
// Created by usatiynyan.
//

#pragma once

#include "sl/io/file.hpp"

#include <sl/meta/enum/flag.hpp>
#include <tl/expected.hpp>

#include <chrono>
#include <span>
#include <sys/epoll.h>

namespace sl::io {

class epoll {
    explicit epoll(int epfd) : handle_{ epfd } {}

public:
    enum class op : int {
        ADD = EPOLL_CTL_ADD,
        MOD = EPOLL_CTL_MOD,
        DEL = EPOLL_CTL_DEL,
    };

    enum class event : std::uint32_t {
        IN = EPOLLIN,
        OUT = EPOLLOUT,
        RDHUP = EPOLLRDHUP,
        PRI = EPOLLPRI,
        ERR = EPOLLERR,
        HUP = EPOLLHUP,
        ET = EPOLLET,
        ONESHOT = EPOLLONESHOT,
        WAKEUP = EPOLLWAKEUP,
        EXCLUSIVE = EPOLLEXCLUSIVE,
    };
    using event_flag = meta::enum_flag<event>;

    [[nodiscard]] static tl::expected<epoll, std::error_code> create();

    [[nodiscard]] tl::expected<tl::monostate, std::error_code> ctl(op an_op, const file& a_file, epoll_event an_event);
    [[nodiscard]] tl::expected<tl::monostate, std::error_code> ctl(op an_op, file::view a_file, epoll_event an_event);
    [[nodiscard]] tl::expected<std::uint32_t, std::error_code>
        wait(std::span<epoll_event> out_events, tl::optional<std::chrono::milliseconds> maybe_timeout);

private:
    file handle_;
};

} // namespace sl::io
