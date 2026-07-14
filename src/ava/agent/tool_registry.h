#pragma once

#include "ava/agent/tool_metadata.h"
#include "ava/agent/tool_types.h"
#include "ava/tools/file_tools.h"
#include "ava/core/result.h"

#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ava::agent {

enum class ToolSource
{
  Builtin,
  Plugin,
  Mcp,
};

[[nodiscard]] std::string_view to_string(ToolSource source) noexcept;

// Built-in handlers receive the full ToolContext. External plugin/MCP tools should register AVA-owned broker
// executors here, not plugin code with direct access to internal safety escape hatches.
using ToolExecutor = std::function<ToolDispatchResult(ava::tools::ToolContext const& context, ProviderToolCall const& call)>;

struct RegisteredToolMetadata
{
  std::string name;
  std::string description;
  std::string schema_json;
  std::string permission_category;
  std::string output_bound_summary;
  std::string execution_mode;
  std::string event_rendering_hint;
  std::optional<std::string> description_family;

  [[nodiscard]] ToolMetadata view() const noexcept;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

[[nodiscard]] RegisteredToolMetadata own_tool_metadata(ToolMetadata const& metadata);

struct RegisteredTool
{
  RegisteredToolMetadata metadata;
  ToolExecutor executor = nullptr;
  ToolSource source = ToolSource::Builtin;
  std::string source_id = "builtin";
  bool brokered_external = false;
  bool requires_lsp_diagnostics = false;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

class ToolRegistry
{
 public:
  [[nodiscard]] ava::core::VoidResult register_tool(RegisteredTool tool);
  void apply_visibility_filter(ava::tools::ToolContext const& context);
  [[nodiscard]] RegisteredTool const* find(std::string_view name) const noexcept;
  [[nodiscard]] std::span<RegisteredTool const> entries() const noexcept;
  [[nodiscard]] std::vector<ToolMetadata> metadata() const;
  [[nodiscard]] std::vector<std::string> tool_schemas_json(ava::tools::ToolContext const& context) const;

  AVA_DEBUG_PRINT_MEMBERS_ON

 private:
  std::vector<RegisteredTool> tools_;
};

[[nodiscard]] ToolRegistry const& builtin_tool_registry();

}  // namespace ava::agent
