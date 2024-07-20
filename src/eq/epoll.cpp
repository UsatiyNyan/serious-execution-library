//
// Created by usatiynyan.
//

#include "sl/eq/epoll.hpp"
#include "sl/eq/handler.hpp"
#include "sl/exec/generic/schedule.hpp"
#include "sl/io/socket.hpp"

#include <libassert/assert.hpp>
#include <sl/meta/lifetime/defer.hpp>

namespace sl::eq {
namespace detail {

::epoll_event make_client_handler(io::epoll& epoll, async_connection an_async_connection) {
    auto* client_handler = allocate_handler( //
        [an_async_connection = std::move(an_async_connection),
         close_connection = meta::defer{ [&epoll, handle = io::file::view{ an_async_connection.handle() }] {
             epoll //
                 .ctl(io::epoll::op::DEL, handle, ::epoll_event{})
                 .map_error([](std::error_code ec) { PANIC(ec); });
         } }] //
        (io::epoll::event_flag events) mutable noexcept {
            constexpr io::epoll::event_flag errors_flag{ EPOLLRDHUP | EPOLLHUP | EPOLLERR };
            if (events & errors_flag) {
                // TODO: can get EPOLLERR from getsockopt
                return handler_result::END;
            }
            if (events & EPOLLIN) {
                an_async_connection.handle_read();
            } else if (events & EPOLLOUT) {
                an_async_connection.handle_write();
            } else {
                PANIC(events, "connection: handle unknown events");
            }
            return handler_result::CONTINUE;
        }
    );
    return ::epoll_event{
        .events = EPOLLIN | EPOLLOUT | EPOLLRDHUP | EPOLLHUP | EPOLLERR | EPOLLET,
        .data{ .ptr = client_handler },
    };
}

} // namespace detail

::epoll_event make_server_handler(
    io::epoll& epoll,
    io::socket::server& server,
    exec::generic_executor& executor,
    make_client_coro_type make_client_coro
) {
    auto* server_handler = allocate_handler( //
        [&epoll,
         &server,
         &executor,
         make_client_coro = std::move(make_client_coro),
         close_server = meta::defer{ [&epoll, handle = io::file::view{ server.handle }] {
             epoll //
                 .ctl(io::epoll::op::DEL, handle, ::epoll_event{})
                 .map_error([](std::error_code ec) { PANIC(ec); });
         } }] //
        (io::epoll::event_flag events) mutable noexcept {
            while (true) {
                auto accept_result = server.accept();

                if (!accept_result.has_value()) {
                    const auto ec = std::move(accept_result).error();
                    if (ec == std::errc::resource_unavailable_try_again || ec == std::errc::operation_would_block) {
                        return handler_result::CONTINUE;
                    } else {
                        PANIC(ec, "TODO: determine when server should be closed");
                        return handler_result::END;
                    }
                }

                auto connection = std::move(accept_result).value();
                const io::file::view handle_view{ connection.handle };

                async_connection an_async_connection{ std::move(connection) };
                async_connection::view an_async_connection_view{ an_async_connection };

                epoll
                    .ctl(
                        io::epoll::op::ADD,
                        handle_view,
                        detail::make_client_handler(epoll, std::move(an_async_connection))
                    )
                    .map_error([](std::error_code ec) { PANIC(ec); });
                exec::schedule(executor, make_client_coro(an_async_connection_view));
            }
        }
    );
    return ::epoll_event{
        .events = EPOLLIN | EPOLLET,
        .data{ .ptr = server_handler },
    };
}

void execute_events(std::span<const epoll_event> events) {
    for (const ::epoll_event& event : events) {
        auto* handler_ptr = static_cast<handler_base*>(event.data.ptr);
        ASSERT(handler_ptr != nullptr);
        const auto result = handler_ptr->execute(io::epoll::event_flag{ event.events });
        if (result == handler_result::END) {
            // assuming that client is removed from epoll here
            // and server communicates to stop executing events
            delete handler_ptr;
        }
    }
}

} // namespace sl::eq
