#pragma once

#include "ava/app/runtime.h"
#include "ava/permissions/permission_rules.h"

#include <string_view>

namespace ava::app {

// Single app-owned source for the persistent permission-rule store bound to a
// runtime session. Every command permission path (direct app commands, model
// tool calls, ACP hosts, RPC, print/line-shell) resolves the same global and
// workspace rule files through this helper instead of duplicating path logic.
[[nodiscard]] ava::permissions::PermissionRuleStore permission_rule_store_for_session(runtime::Session const& session);

[[nodiscard]] ava::core::Result<runtime::Session> create_runtime_session_like(runtime::Session const& current, runtime::OpenOptions const& base_options);
[[nodiscard]] ava::core::Result<runtime::Session> open_runtime_session_like(runtime::Session const& current, runtime::OpenOptions const& base_options,
                                                                          std::string_view requested_session_id);
[[nodiscard]] ava::core::Result<runtime::Session> create_runtime_session_at(runtime::OpenOptions base_options, std::filesystem::path const& workspace_root,
                                                                          std::filesystem::path const& current_dir);
[[nodiscard]] ava::core::Result<runtime::Session> open_runtime_session_at(runtime::OpenOptions base_options, std::filesystem::path const& workspace_root,
                                                                        std::filesystem::path const& current_dir, std::string_view requested_session_id);

}  // namespace ava::app
