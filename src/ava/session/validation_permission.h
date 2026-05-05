#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>
#include <vector>

#include "ava/session/session_store.h"
#include "ava/session/validation.h"

namespace ava::session {

struct SessionReplayPendingPermissionPrompt {
  std::string entry_id;
  std::size_t entry_index = 0;
};

using SessionReplayPendingPermissions =
    std::unordered_map<std::string, std::vector<SessionReplayPendingPermissionPrompt>>;

void validate_permission_decision(SessionReplayValidation& validation, SessionReplayPendingPermissions& pending,
                                  std::size_t index, SessionEntry const& entry);
void add_compaction_permission_boundary_issues(SessionReplayValidation& validation,
                                               SessionReplayPendingPermissions const& pending, std::size_t index,
                                               SessionEntry const& entry);
void add_unresolved_permission_prompts(SessionReplayValidation& validation,
                                       SessionReplayPendingPermissions const& pending, std::size_t entry_index);

}  // namespace ava::session
