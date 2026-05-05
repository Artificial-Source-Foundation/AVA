#include "ava/session/validation_issue.h"

#include <utility>

namespace ava::session {

void add_replay_issue(SessionReplayValidation& validation, SessionReplayIssue issue)
{
  if (issue.severity == SessionReplayIssueSeverity::Warning) {
    ++validation.warning_count;
  } else {
    ++validation.error_count;
  }
  validation.issues.push_back(std::move(issue));
}

void add_replay_error(SessionReplayValidation& validation, SessionReplayIssueKind kind, std::size_t index,
                      SessionEntry const& entry, std::string call_id, std::string message)
{
  add_replay_issue(validation, SessionReplayIssue{.severity = SessionReplayIssueSeverity::Error,
                                                  .kind = kind,
                                                  .entry_index = index,
                                                  .entry_id = entry.id,
                                                  .call_id = std::move(call_id),
                                                  .message = std::move(message)});
}

}  // namespace ava::session
