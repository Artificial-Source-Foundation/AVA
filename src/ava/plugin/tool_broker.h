#pragma once

#include "ava/agent/tool_registry.h"

#include "ava/plugin/runner.h"

#include "ava/tools/file_tools.h"

#include <string>
#include <string_view>

namespace ava::plugin {

[[nodiscard]] std::string plugin_model_tool_name(std::string_view plugin_id, std::string_view tool_name);
[[nodiscard]] PluginProxyHandler make_core_service_proxy_handler(ava::tools::ToolContext context,
                                                                 PluginManifest manifest,
                                                                 std::string contribution_kind,
                                                                 std::string contribution_name,
                                                                 std::string model_operation_name,
                                                                 std::string call_id);
void register_enabled_plugin_tools(ava::agent::ToolRegistry& registry, ava::tools::ToolContext const& context);

}  // namespace ava::plugin
