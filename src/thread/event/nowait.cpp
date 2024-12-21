//
// Created by usatiynyan.
//

#include "sl/exec/thread/event/nowait.hpp"

#include <libassert/assert.hpp>

namespace sl::exec {

void nowait_event::set() { is_set_ = true; }

void nowait_event::wait() { ASSERT(is_set_); }

} // namespace sl::exec
