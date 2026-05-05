#pragma once

#include <cstddef>
#include <string>

#include "ava/session/validation.h"

namespace ava::session {

void add_replay_issue(SessionReplayValidation& validation, SessionReplayIssue issue);
void add_replay_error(SessionReplayValidation& validation, SessionReplayIssueKind kind, std::size_t index,
                      SessionEntry const& entry, std::string call_id, std::string message);

}  // namespace ava::session
