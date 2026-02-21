//
// Created by usatiynyan.
//

#pragma once

#include <sl/meta/monad/result.hpp>

#include <cstddef>
#include <system_error>

namespace sl::exec::sim {

struct platform {
    static meta::result<platform, std::error_code> make();

public:
    std::size_t page_size = 0;
};

} // namespace sl::exec::sim
