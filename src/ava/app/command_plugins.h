#pragma once

#include "ava/app/commands.h"

namespace ava::app {

[[nodiscard]] ava::core::Result<CommandResult> run_plugins_command(RuntimeSession& session,
                                                                   const CommandRequest& request);
[[nodiscard]] ava::core::Result<CommandResult> run_plugin_command(RuntimeSession& session,
                                                                  const CommandRequest& request);

}  // namespace ava::app
