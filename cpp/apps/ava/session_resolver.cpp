#include "session_resolver.hpp"

#include <stdexcept>

namespace ava::app {

ResolvedSessionStartup resolve_startup_session(
    ava::session::SessionManager& manager,
    bool resume_latest,
    const std::optional<std::string>& session_id
) {
  if(resume_latest && session_id.has_value()) {
    throw std::invalid_argument("--continue and --session cannot be combined");
  }

  if(session_id.has_value()) {
    const auto loaded = manager.get(*session_id);
    if(!loaded.has_value()) {
      throw std::runtime_error("session not found: " + *session_id);
    }
    return ResolvedSessionStartup{.session = *loaded, .kind = SessionStartupKind::ContinueById};
  }

  if(resume_latest) {
    const auto recent = manager.list_recent(1);
    if(!recent.empty()) {
      return ResolvedSessionStartup{.session = recent.front(), .kind = SessionStartupKind::ContinueLatest};
    }
  }

  return ResolvedSessionStartup{.session = manager.create(), .kind = SessionStartupKind::New};
}

}  // namespace ava::app
