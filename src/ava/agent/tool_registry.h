#pragma once

#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "ava/agent/tool_metadata.h"
#include "ava/agent/tool_types.h"
#include "ava/core/result.h"
#include "ava/tools/file_tools.h"

namespace ava::agent {

enum class ToolSource {
  Builtin,
  Plugin,
  Mcp,
};

[[nodiscard]] std::string_view to_string(ToolSource source) noexcept;

// Built-in handlers receive the full ToolContext. External plugin/MCP tools should register AVA-owned broker
// executors here, not plugin code with direct access to internal safety escape hatches.
using ToolExecutor =
    std::function<ToolDispatchResult(const ava::tools::ToolContext& context, const ProviderToolCall& call)>;

struct RegisteredToolMetadata {
  std::string name;
  std::string description;
  std::string schema_json;
  std::string permission_category;
  std::string output_bound_summary;
  std::string execution_mode;
  std::string event_rendering_hint;
  std::optional<std::string> description_family;

  [[nodiscard]] ToolMetadata view() const noexcept;
};

[[nodiscard]] RegisteredToolMetadata own_tool_metadata(const ToolMetadata& metadata);

struct RegisteredTool {
  RegisteredToolMetadata metadata;
  ToolExecutor executor = nullptr;
  ToolSource source = ToolSource::Builtin;
  std::string source_id = "builtin";
  bool brokered_external = false;
  bool requires_lsp_diagnostics = false;
};

class ToolRegistry {
 public:
  [[nodiscard]] ava::core::VoidResult register_tool(RegisteredTool tool);
  [[nodiscard]] const RegisteredTool* find(std::string_view name) const noexcept;
  [[nodiscard]] std::span<const RegisteredTool> entries() const noexcept;
  [[nodiscard]] std::vector<ToolMetadata> metadata() const;
  [[nodiscard]] std::vector<std::string> tool_schemas_json(const ava::tools::ToolContext& context) const;

 private:
  std::vector<RegisteredTool> tools_;
};

[[nodiscard]] const ToolRegistry& builtin_tool_registry();

}  // namespace ava::agent
