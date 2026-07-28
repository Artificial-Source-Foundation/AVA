#pragma once

#include "ava/agent/tool_registry.h"

namespace ava::agent {

[[nodiscard]] ava::core::Result<ToolRegistry> compose_tool_registry(ava::tools::ToolContext const& context, ToolVisibilityOptions const& visibility);
[[nodiscard]] ToolRegistry compose_tool_registry_or_empty(ava::tools::ToolContext const& context, ToolVisibilityOptions const& visibility);

}  // namespace ava::agent
