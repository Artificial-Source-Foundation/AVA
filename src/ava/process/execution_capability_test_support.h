#pragma once

#include "ava/debug/print_members_on.h"

#include <functional>

namespace ava::process::testing {

enum class CapabilityMintHookStage
{
  AfterBeforeObservation,
  AfterOpenedObservation,
};

// Deterministic mint-race seam. Hooks run without the internal hook lock and
// receive only a closed stage value, never a path, descriptor, or identity.
class ExecutionCapabilityTestAccess final
{
 public:
  static void set_mint_hook(std::function<void(CapabilityMintHookStage)> hook);
  static void clear_mint_hook() noexcept;

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

}  // namespace ava::process::testing
