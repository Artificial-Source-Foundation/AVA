#pragma once

#include "ava/app/commands.h"

#include <string_view>

namespace ava::app {

[[nodiscard]] ava::core::Result<CommandResult> run_models_command(RuntimeSession& session, std::string_view query = {});

}  // namespace ava::app
