#pragma once

#include "ava/app/command_registry.h"
#include "ava/app/commands.h"

namespace ava::app {

[[nodiscard]] ava::core::Result<CommandResult> run_mcp_command(runtime::session_ts& unlocked_session, CommandRequest const& request);

// Load the MCP prompt identified by `entry`, parsing `argument_text` and enforcing permissions from `request`.
//
// Returns the prompt text supplied by the configured MCP server. The session wrapper is locked only for short state snapshots and tool-context creation;
// server startup and protocol I/O run without holding the session lock.
[[nodiscard]] ava::core::Result<std::string> mcp_prompt_message(runtime::session_ts& unlocked_session, CommandRequest const& request,
                                                                CommandRegistryEntry const& entry, std::string_view argument_text);

}  // namespace ava::app
