#pragma once

#include "ava/app/commands.h"

#include <string_view>

namespace ava::app {

[[nodiscard]] ava::core::Result<CommandResult> run_sessions_command(runtime::Session& session, std::string_view query = {});
[[nodiscard]] ava::core::Result<CommandResult> run_fork_command(runtime::Session& session, std::string_view name = {});
[[nodiscard]] ava::core::Result<CommandResult> run_clone_command(runtime::Session& session, std::string_view name = {});
[[nodiscard]] ava::core::Result<CommandResult> run_new_session_command(runtime::Session& session, std::string_view name = {});
[[nodiscard]] ava::core::Result<CommandResult> run_resume_command(runtime::Session& session, std::string_view session_id);
[[nodiscard]] ava::core::Result<CommandResult> run_name_command(runtime::Session& session, std::string_view name);
[[nodiscard]] ava::core::Result<CommandResult> run_labels_command(runtime::Session& session, std::string_view labels);
[[nodiscard]] ava::core::Result<CommandResult> run_mode_command(runtime::Session& session);
[[nodiscard]] ava::core::Result<CommandResult> run_context_command(runtime::Session& session, std::string_view query = {});
[[nodiscard]] ava::core::Result<CommandResult> run_stats_command(runtime::Session& session);
[[nodiscard]] ava::core::Result<CommandResult> run_compact_command(runtime::Session& session, CommandRequest const& request);
[[nodiscard]] ava::core::Result<CommandResult> run_import_command(runtime::Session& session, std::string_view argument);
[[nodiscard]] ava::core::Result<CommandResult> run_export_command(runtime::Session& session, CommandRequest const& request);

}  // namespace ava::app
