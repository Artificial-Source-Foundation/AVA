#pragma once

#include "ava/app/runtime.h"
#include "ava/permissions/permission_rules.h"

#include <string_view>
#include <vector>

namespace ava::app {

// Single app-owned authority boundary for local command planning. Keep this
// short, stable list in sync for direct commands and model ToolContexts.
[[nodiscard]] std::vector<std::filesystem::path> command_authority_roots_for_session(runtime::Session const& session);

[[nodiscard]] ava::core::Result<runtime::Session> create_runtime_session_like(runtime::Session const& current, runtime::OpenOptions const& base_options);
[[nodiscard]] ava::core::Result<runtime::Session> open_runtime_session_like(runtime::Session const& current, runtime::OpenOptions const& base_options,
                                                                          std::string_view requested_session_id);
[[nodiscard]] ava::core::Result<runtime::Session> create_runtime_session_at(runtime::OpenOptions base_options, std::filesystem::path const& workspace_root,
                                                                          std::filesystem::path const& current_dir);
[[nodiscard]] ava::core::Result<runtime::Session> open_runtime_session_at(runtime::OpenOptions base_options, std::filesystem::path const& workspace_root,
                                                                        std::filesystem::path const& current_dir, std::string_view requested_session_id);

}  // namespace ava::app
