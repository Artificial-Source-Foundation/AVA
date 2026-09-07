#pragma once

#include "ava/process/execution_capability.h"

#include <optional>
#include <string_view>

namespace ava::process::detail {

// The only bridge from opaque public capabilities to the process backend.
// It exposes native authority solely inside ava_process and never through test
// support or a public accessor.
class ExecutionCapabilityAccess final
{
 public:
  [[nodiscard]] static bool logical_matches(PreopenedExecutableV1 const& capability, std::string_view logical_path) noexcept;
  [[nodiscard]] static bool logical_matches(AnchoredWorkingDirectoryV1 const& capability, std::string_view logical_path) noexcept;

  [[nodiscard]] static ava::core::VoidResult refresh(PreopenedExecutableV1 const& capability);
  [[nodiscard]] static ava::core::VoidResult refresh(AnchoredWorkingDirectoryV1 const& capability);
  [[nodiscard]] static ava::core::VoidResult refresh_spawn(std::optional<PreopenedExecutableV1> const& executable, std::string_view executable_logical,
                                                           std::optional<AnchoredWorkingDirectoryV1> const& cwd, std::string_view cwd_logical);

  [[nodiscard]] static int target_descriptor(PreopenedExecutableV1 const& capability) noexcept;
  [[nodiscard]] static int target_descriptor(AnchoredWorkingDirectoryV1 const& capability) noexcept;

  // Called only in the post-fork child after fchdir. It closes inherited cwd,
  // route, and AnchorSet descriptors without freeing allocator-backed state.
  static void child_close_after_fchdir(AnchoredWorkingDirectoryV1& capability) noexcept;

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

}  // namespace ava::process::detail
