#pragma once

#include "ava/app/commands.h"

#include <string_view>

namespace ava::app {

[[nodiscard]] ava::core::Result<CommandResult> run_sessions_command(RuntimeSession& session, std::string_view query = {});
[[nodiscard]] ava::core::Result<CommandResult> run_fork_command(RuntimeSession& session, std::string_view name = {});
[[nodiscard]] ava::core::Result<CommandResult> run_clone_command(RuntimeSession& session, std::string_view name = {});
[[nodiscard]] ava::core::Result<CommandResult> run_new_session_command(RuntimeSession& session, std::string_view name = {});
[[nodiscard]] ava::core::Result<CommandResult> run_resume_command(RuntimeSession& session, std::string_view session_id);
[[nodiscard]] ava::core::Result<CommandResult> run_name_command(RuntimeSession& session, std::string_view name);
[[nodiscard]] ava::core::Result<CommandResult> run_labels_command(RuntimeSession& session, std::string_view labels);
[[nodiscard]] ava::core::Result<CommandResult> run_mode_command(RuntimeSession& session);
[[nodiscard]] ava::core::Result<CommandResult> run_context_command(RuntimeSession& session, std::string_view query = {});
[[nodiscard]] ava::core::Result<CommandResult> run_stats_command(RuntimeSession& session);
[[nodiscard]] ava::core::Result<CommandResult> run_compact_command(RuntimeSession& session, CommandRequest const& request);
[[nodiscard]] ava::core::Result<CommandResult> run_export_command(RuntimeSession& session);

}  // namespace ava::app
