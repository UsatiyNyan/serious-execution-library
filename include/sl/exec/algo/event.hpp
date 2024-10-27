//
// Created by usatiynyan.
//

#pragma once

#include "sl/exec/algo/event/atomic.hpp"
#include "sl/exec/algo/event/mutex.hpp"
#include "sl/exec/algo/event/nowait.hpp"

namespace sl::exec {

// TODO: add other OS-es
using default_event = atomic_event;

} // namespace sl::exec
