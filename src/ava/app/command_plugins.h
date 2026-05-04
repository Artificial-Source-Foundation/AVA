#pragma once

#include "ava/app/commands.h"

namespace ava::app {

[[nodiscard]] ava::core::Result<CommandResult> run_plugins_command(RuntimeSession& session,
                                                                   CommandRequest const& request);
[[nodiscard]] ava::core::Result<CommandResult> run_plugin_command(RuntimeSession& session,
                                                                  CommandRequest const& request);

}  // namespace ava::app
