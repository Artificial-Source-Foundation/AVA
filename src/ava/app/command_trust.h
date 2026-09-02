#pragma once

#include "ava/debug/print_members_on.h"
#include "ava/app/commands.h"
#include "ava/app/project_trust.h"
#include "ava/core/error.h"

#include <optional>
#include <string_view>

namespace ava::app {

enum class ProjectTrustOperation
{
  Reload,
  Enable,
  Deny,
  Clear,
};

struct ProjectTrustApplyResult
{
  ProjectTrustState state;
  bool authority_retired = false;
  bool prompt_fail_closed = false;
  std::optional<ava::core::Error> prompt_warning = std::nullopt;

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

// Shared /trust and /reload trust transaction. Effective untrusted states use
// the process-local delivery-manager workspace barrier; trusted states retain
// the ordinary current-controller refresh path.
[[nodiscard]] ava::core::Result<ProjectTrustApplyResult> apply_project_trust_operation(runtime::session_ts& unlocked_session, ProjectTrustOperation operation);

[[nodiscard]] ava::core::Result<CommandResult> run_trust_command(runtime::session_ts& unlocked_session, std::string_view argument);

}  // namespace ava::app
