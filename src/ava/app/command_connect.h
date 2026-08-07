#pragma once

#include "ava/app/commands.h"

namespace ava::app {

[[nodiscard]] ava::core::Result<CommandResult> run_connect_command(runtime::session_ts& session, CommandRequest const& request);

}  // namespace ava::app
