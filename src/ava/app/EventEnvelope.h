#pragma once

#include "ava/event/EventEnvelope.h"

namespace ava::app {

// Migration-only alias; consumers will move to ava::event in a later checkpoint.
using EventEnvelope = ava::event::EventEnvelope;

}  // namespace ava::app
