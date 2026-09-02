#pragma once

#include "ava/debug/print_members_on.h"

#include <functional>

namespace ava::core::testing {

// Deterministic external-target replacement seam. The callback is copied
// under an internal lock and invoked without that lock after descriptor-based
// inspection but before the physical-parent final reopen. No path, descriptor,
// or identity data crosses this boundary.
class AnchorOpenTestAccess final
{
 public:
  static void set_before_external_reopen_hook(std::function<void()> hook);
  static void clear_before_external_reopen_hook() noexcept;

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

}  // namespace ava::core::testing
