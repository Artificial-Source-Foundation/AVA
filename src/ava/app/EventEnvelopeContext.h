#pragma once

#include "ava/event/EventEnvelopeContext.h"

namespace ava::app {

// Migration-only alias; consumers will move to ava::event in a later checkpoint.
using EventEnvelopeContext = ava::event::EventEnvelopeContext;

}  // namespace ava::app
