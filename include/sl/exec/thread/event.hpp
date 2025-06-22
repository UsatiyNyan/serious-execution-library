//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/thread/event/atomic.hpp"
#include "sl/exec/thread/event/mutex.hpp"
#include "sl/exec/thread/event/nowait.hpp"

namespace sl::exec {

using default_event = atomic_event<detail::atomic>;

} // namespace sl::exec
