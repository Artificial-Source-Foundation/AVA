#pragma once

#include "ava/debug/print_members_on.h"
#include "ava/process/supervisor.h"

#include <functional>

namespace ava::process::testing {

// Narrow per-supervisor deterministic race seam. Production code does not
// include this header and a Supervisor owns any installed callback.
class SupervisorTestAccess final
{
 public:
  static void set_after_fork_before_release_hook(Supervisor& supervisor, std::function<void()> hook);
  static void clear_after_fork_before_release_hook(Supervisor& supervisor) noexcept;
  static void set_after_completion_channel_create_hook(Supervisor& supervisor, std::function<void()> hook);
  static void clear_after_completion_channel_create_hook(Supervisor& supervisor) noexcept;
  static void fail_next_common_child_working_directory(Supervisor& supervisor) noexcept;

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

}  // namespace ava::process::testing
