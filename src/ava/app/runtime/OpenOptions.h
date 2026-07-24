#pragma once

#include "RuntimeContinuity.h"
#include "SessionOpenRequest.h"

namespace ava::app::runtime {

// Collect application continuity and one-open request state without allowing
// session replacement callers to hand-copy either group field by field.
struct OpenOptions
{
  RuntimeContinuity continuity;
  SessionOpenRequest request;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

}  // namespace ava::app::runtime
