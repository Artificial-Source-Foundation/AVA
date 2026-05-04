#pragma once

#include "ava/app/commands.h"

namespace ava::app {

[[nodiscard]] ava::core::Result<CommandResult> run_mcp_command(RuntimeSession& session, const CommandRequest& request);

}  // namespace ava::app
