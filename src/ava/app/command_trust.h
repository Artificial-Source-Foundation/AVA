#pragma once

#include "ava/app/commands.h"

#include <string_view>

namespace ava::app {

[[nodiscard]] ava::core::Result<CommandResult> run_trust_command(runtime::session_ts& unlocked_session, std::string_view argument);

}  // namespace ava::app
