#pragma once

#include "ava/app/commands.h"

namespace ava::app {

[[nodiscard]] ava::core::Result<CommandResult> run_mcp_command(runtime::RuntimeSession& session, CommandRequest const& request);

}  // namespace ava::app
