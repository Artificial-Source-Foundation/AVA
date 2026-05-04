#pragma once

#include <string_view>

#include "ava/app/commands.h"

namespace ava::app {

[[nodiscard]] ava::core::Result<CommandResult> run_sessions_command(RuntimeSession& session,
                                                                    std::string_view query = {});
[[nodiscard]] ava::core::Result<CommandResult> run_mode_command(RuntimeSession& session);
[[nodiscard]] ava::core::Result<CommandResult> run_context_command(RuntimeSession& session,
                                                                   std::string_view query = {});
[[nodiscard]] ava::core::Result<CommandResult> run_stats_command(RuntimeSession& session);
[[nodiscard]] ava::core::Result<CommandResult> run_compact_command(RuntimeSession& session,
                                                                   CommandRequest const& request);
[[nodiscard]] ava::core::Result<CommandResult> run_export_command(RuntimeSession& session);

}  // namespace ava::app
