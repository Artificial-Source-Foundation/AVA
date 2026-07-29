#pragma once

#include "ava/app/runtime.h"
#include "ava/permissions/permission_rules.h"

#include <string_view>
#include <vector>

namespace ava::app {

// Single app-owned authority boundary for local command planning. Keep this
// short, stable list in sync for direct commands and model ToolContexts.
[[nodiscard]] std::vector<std::filesystem::path> command_authority_roots_for_session(runtime::Session const& session);

// Create a persistent session inheriting active state from `current` and
// frontend-only policy from `base_context`.
[[nodiscard]] ava::core::Result<runtime::Session> create_runtime_session_like(runtime::Session const& current, runtime::RuntimeOpenContext const& base_context);

// Open a session using `request`, inheriting active state from `current` and
// frontend-only policy from `base_context`.
[[nodiscard]] ava::core::Result<runtime::Session> open_runtime_session_like(runtime::Session const& current, runtime::RuntimeOpenContext const& base_context,
                                                                            runtime::SessionLifecycleRequest request);

// Create a persistent session at `workspace_root` and `current_dir`, overriding
// those locations in `context`.
[[nodiscard]] ava::core::Result<runtime::Session> create_runtime_session_at(runtime::RuntimeOpenContext context, std::filesystem::path const& workspace_root,
                                                                            std::filesystem::path const& current_dir);

// Open a session using `request` at `workspace_root` and `current_dir`,
// overriding those locations in `context`.
[[nodiscard]] ava::core::Result<runtime::Session> open_runtime_session_at(runtime::RuntimeOpenContext context, std::filesystem::path const& workspace_root,
                                                                          std::filesystem::path const& current_dir, runtime::SessionLifecycleRequest request);

}  // namespace ava::app
