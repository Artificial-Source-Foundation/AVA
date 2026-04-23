#pragma once

#include <optional>
#include <string>

#include "ava/session/session.hpp"

namespace ava::app {

enum class SessionStartupKind {
  New,
  ContinueLatest,
  ContinueById,
};

struct ResolvedSessionStartup {
  ava::types::SessionRecord session;
  SessionStartupKind kind{SessionStartupKind::New};
};

[[nodiscard]] ResolvedSessionStartup resolve_startup_session(
    ava::session::SessionManager& manager,
    bool resume_latest,
    const std::optional<std::string>& session_id
);

}  // namespace ava::app
