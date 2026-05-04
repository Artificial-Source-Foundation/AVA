#pragma once

#include <string>
#include <string_view>

#include "ava/agent/tool_registry.h"
#include "ava/tools/file_tools.h"

namespace ava::plugin {

[[nodiscard]] std::string plugin_model_tool_name(std::string_view plugin_id, std::string_view tool_name);
void register_enabled_plugin_tools(ava::agent::ToolRegistry& registry, const ava::tools::ToolContext& context);

}  // namespace ava::plugin
