//
// Created by usatiynyan.
//

#pragma once

#include <sl/meta/lifetime/immovable.hpp>

#include <tl/expected.hpp>
#include <tl/optional.hpp>

#include <cstddef>
#include <span>
#include <system_error>
#include <utility>

namespace sl::io {

class file : public meta::immovable {
public:
    explicit file(int fd) : fd_{ tl::in_place, fd } {}
    file(file&& other) : fd_{ std::exchange(other.fd_, tl::nullopt) } {}
    ~file() noexcept;

    [[nodiscard]] int internal() const { return *fd_; }
    [[nodiscard]] int release() && { return *std::exchange(fd_, tl::nullopt); }

    [[nodiscard]] tl::expected<std::uint32_t, std::error_code> read(std::span<std::byte> buffer);
    [[nodiscard]] tl::expected<std::uint32_t, std::error_code> write(std::span<const std::byte> buffer);

private:
    tl::optional<int> fd_{};
};

} // namespace sl::io
