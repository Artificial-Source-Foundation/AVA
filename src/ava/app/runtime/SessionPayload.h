#pragma once

#include "ava/debug/print_members_on.h"
#include "ava/core/mode.h"

#include <string>

namespace ava::app::runtime {

// Extracted session-start slice of an Event: the active mode plus the provider/model identity published when a run begins.
struct SessionPayload
{
  ava::core::Mode mode = ava::core::Mode::Build;
  std::string provider;
  std::string model;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

}  // namespace ava::app::runtime
