#pragma once

#include <string_view>

#include "ava/app/commands.h"

namespace ava::app {

[[nodiscard]] ava::core::Result<CommandResult> run_models_command(RuntimeSession& session,
                                                                  std::string_view query = {});

}  // namespace ava::app
