//
// Created by usatiynyan.
//

#pragma once

#include <atomic>
#include <cstdint>

namespace sl::exec {

class wait_group {
public:
    void add(std::uint32_t count);
    void done();
    void wait();

private:
    std::atomic<std::uint32_t> work_{ 0 };
};

} // namespace sl::exec
