#include "sl/eq/epoll.hpp"
#include <sl/eq.hpp>
#include <sl/exec.hpp>
#include <sl/io.hpp>

#include <iostream>
#include <libassert/assert.hpp>

namespace exec = sl::exec;
namespace io = sl::io;
namespace eq = sl::eq;

auto parse_args(int argc, const char* argv[]) {
    ASSERT(argc == 3);
    return std::make_tuple(
        static_cast<std::uint16_t>(std::strtoul(argv[1], nullptr, 10)),
        static_cast<std::uint16_t>(std::strtoul(argv[2], nullptr, 10))
    );
}

int main(int argc, const char* argv[]) {
    const auto [port, max_clients] = parse_args(argc, argv);

    exec::st::executor executor;
    auto epoll = *ASSERT_VAL(io::epoll::create());

    io::socket::server server =
        io::socket::create(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0)
            .and_then([](io::socket socket) {
                return socket.set_opt(SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, 1)
                    .map([socket = std::move(socket)](tl::monostate) mutable { return std::move(socket); });
            })
            .and_then([_port = port](io::socket socket) { return std::move(socket).bind(AF_INET, _port, INADDR_ANY); })
            .and_then([_max_clients = max_clients](io::socket::bound_server bound_server) {
                return std::move(bound_server).listen(_max_clients);
            })
            .map_error([](std::error_code ec) { PANIC(ec); })
            .value();

    eq::setup_server_handler(epoll, server, executor, [](eq::async_connection::view conn) -> exec::async<void> {
        std::cout << "start client\n";
        while (true) {
            std::array<std::byte, 1024> buffer{};

            const auto read_result = co_await conn.read(buffer);
            if (!read_result.has_value() || *read_result == 0) {
                break;
            }
            std::string_view string_view{ reinterpret_cast<char*>(buffer.data()), *read_result };
            std::cout << "got:{" << string_view << "}\n";

            const auto write_result = co_await conn.write(std::span{ buffer.data(), *read_result });
            if (!write_result.has_value() || *write_result == 0) {
                break;
            }
            std::cout << "read: " << *read_result << " written: " << *write_result << "\n";
        }
        std::cout << "end client\n";
    });

    std::array<::epoll_event, 1024> events{};
    while (const auto wait_result = epoll.wait(events, tl::nullopt)) {
        const std::uint32_t nevents = wait_result.value();
        eq::execute_events(std::span{ events.data(), nevents });
        while (executor.execute_batch() > 0) {}
    }

    return 0;
}
