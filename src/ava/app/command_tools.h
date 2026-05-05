#pragma once

#include <string>

#include "ava/app/command_tool_events.h"
#include "ava/app/commands.h"
#include "ava/tools/file_tools.h"

namespace ava::app {

[[nodiscard]] ava::tools::ToolContext make_tool_context(RuntimeSession& session,
                                                        ava::permissions::PermissionResolver permission_resolver);

[[nodiscard]] ava::core::Result<CommandResult> run_tool_command(RuntimeSession& session, CommandRequest& request);

}  // namespace ava::app
