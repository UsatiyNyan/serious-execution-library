//
// Created by usatiynyan.
//

#include "sl/io/epoll.hpp"

#include "sl/io/detail.hpp"

#include <function2/function2.hpp>
#include <libassert/assert.hpp>

namespace sl::io {

tl::expected<epoll, std::error_code> epoll::create() {
    const int epfd = epoll_create1(0);
    if (epfd == -1) {
        return tl::make_unexpected(detail::make_error_code_from_errno());
    }
    return epoll{ epfd };
}

tl::expected<tl::monostate, std::error_code> epoll::ctl(op an_op, const file& a_file, epoll_event an_event) {
    const int result = epoll_ctl(handle_.internal(), static_cast<int>(an_op), a_file.internal(), &an_event);
    if (result == -1) {
        return tl::make_unexpected(detail::make_error_code_from_errno());
    }
    return tl::monostate{};
}

tl::expected<std::uint32_t, std::error_code>
    epoll::wait(std::span<epoll_event> out_events, tl::optional<std::chrono::milliseconds> maybe_timeout) {
    ASSERT(out_events.size() > 0);
    const int timeout_ms = maybe_timeout //
                               .map([](std::chrono::milliseconds x) { return static_cast<int>(x.count()); })
                               .value_or(-1);
    const int nfds = epoll_wait( //
        handle_.internal(),
        out_events.data(),
        static_cast<int>(out_events.size()),
        timeout_ms
    );
    if (nfds == -1) {
        return tl::make_unexpected(detail::make_error_code_from_errno());
    }
    ASSERT(nfds > 0);
    return static_cast<std::uint32_t>(nfds);
}
} // namespace sl::io
