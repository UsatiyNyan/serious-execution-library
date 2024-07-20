//
// Created by usatiynyan.
//

#pragma once

#include "sl/eq/async_connection.hpp"
#include "sl/exec/generic/async.hpp"
#include "sl/exec/generic/executor.hpp"
#include "sl/io/epoll.hpp"
#include "sl/io/socket.hpp"
#include <function2/function2.hpp>

namespace sl::eq {

using make_client_coro_type = fu2::function_base<
    /*IsOwning=*/true,
    /*IsCopyable=*/false,
    /*Capacity=*/fu2::capacity_default,
    /*IsThrowing=*/false,
    /*HasStrongExceptGuarantee=*/true,
    exec::async<void>(async_connection::view async_connection_view)>;

::epoll_event make_server_handler(
    io::epoll& epoll,
    io::socket::server& server,
    exec::generic_executor& executor,
    make_client_coro_type make_client_coro
);

void execute_events(std::span<const epoll_event> events);

} // namespace sl::eq
