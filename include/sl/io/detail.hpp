//
// Created by usatiynyan.
//

#pragma once

#include <system_error>

namespace sl::io::detail {

inline std::error_code make_error_code_from_errno() { return std::make_error_code(std::errc{ errno }); }

} // namespace sl::io::detail
