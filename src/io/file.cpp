//
// Created by usatiynyan.
//

#include "sl/io/file.hpp"

#include "sl/io/detail.hpp"

#include <libassert/assert.hpp>
#include <unistd.h>

namespace sl::io {

file::~file() noexcept {
    if (!fd_.has_value()) {
        return;
    }
    const int fd = fd_.value();
    const int result = close(fd);
    ASSERT(result == 0, detail::make_error_code_from_errno());
}

tl::expected<std::uint32_t, std::error_code> file::read(std::span<std::byte> buffer) {
    ASSERT(fd_.has_value());
    const int nbytes = ::read(fd_.value(), buffer.data(), buffer.size());
    if (nbytes == -1) {
        return tl::make_unexpected(detail::make_error_code_from_errno());
    }
    ASSERT(nbytes > 0);
    return static_cast<std::uint32_t>(nbytes);
}

tl::expected<std::uint32_t, std::error_code> file::write(std::span<const std::byte> buffer) {
    ASSERT(fd_.has_value());
    const int nbytes = ::write(fd_.value(), buffer.data(), buffer.size());
    if (nbytes == -1) {
        return tl::make_unexpected(detail::make_error_code_from_errno());
    }
    ASSERT(nbytes > 0);
    return static_cast<std::uint32_t>(nbytes);
}

} // namespace sl::io
