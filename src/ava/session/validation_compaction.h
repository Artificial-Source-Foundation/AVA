#pragma once

#include <cstddef>

#include "ava/session/session_store.h"
#include "ava/session/validation.h"
#include "ava/session/validation_permission.h"
#include "ava/session/validation_tool.h"

namespace ava::session {

void validate_compaction_entry(SessionReplayValidation& validation, std::size_t index, SessionEntry const& entry);
void validate_compaction_boundaries(SessionReplayValidation& validation, std::size_t index, SessionEntry const& entry,
                                    SessionReplayToolCalls const& tool_calls,
                                    SessionReplayPendingPermissions const& pending_permissions,
                                    SessionReplayValidationOptions const& options);

}  // namespace ava::session
