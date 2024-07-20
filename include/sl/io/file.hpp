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
    class view;

public:
    explicit file(int fd) : fd_{ tl::in_place, fd } {}
    file(file&& other) : fd_{ std::exchange(other.fd_, tl::nullopt) } {}
    ~file() noexcept;

    [[nodiscard]] int internal() const { return *fd_; }
    [[nodiscard]] int release() && { return *std::exchange(fd_, tl::nullopt); }

    [[nodiscard]] tl::expected<std::uint32_t, std::error_code> read(std::span<std::byte> buffer);
    [[nodiscard]] tl::expected<std::uint32_t, std::error_code> write(std::span<const std::byte> buffer);

    [[nodiscard]] tl::expected<std::int32_t, std::error_code> fcntl(std::int32_t op, std::int32_t arg);

private:
    tl::optional<int> fd_{};
};

class file::view {
public:
    explicit view(int fd) : fd_{ fd } {}
    explicit view(const file& a_file) : fd_{ a_file.internal() } {}

    [[nodiscard]] int internal() const { return fd_; }

private:
    int fd_;
};

} // namespace sl::io
