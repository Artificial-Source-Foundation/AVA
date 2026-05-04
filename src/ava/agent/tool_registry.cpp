#include "ava/agent/tool_registry.h"

#include <algorithm>
#include <string>
#include <utility>

#include "ava/core/error.h"
#include "ava/core/json.h"

namespace ava::agent {
namespace {

std::optional<std::string> tool_name_from_schema(std::string_view schema_json)
{
  if (auto name = ava::core::json::string_field(schema_json, "name")) return name;
  if (auto function = ava::core::json::object_field(schema_json, "function")) {
    return ava::core::json::string_field(*function, "name");
  }
  return std::nullopt;
}

}  // namespace

std::string_view to_string(ToolSource source) noexcept
{
  switch (source) {
    case ToolSource::Builtin:
      return "builtin";
    case ToolSource::Plugin:
      return "plugin";
    case ToolSource::Mcp:
      return "mcp";
  }
  return "unknown";
}

ToolMetadata RegisteredToolMetadata::view() const noexcept
{
  return ToolMetadata{.name = name,
                      .description = description,
                      .schema_json = schema_json,
                      .permission_category = permission_category,
                      .output_bound_summary = output_bound_summary,
                      .execution_mode = execution_mode,
                      .event_rendering_hint = event_rendering_hint,
                      .description_family = description_family
                                                ? std::optional<std::string_view>(std::string_view(*description_family))
                                                : std::nullopt};
}

RegisteredToolMetadata own_tool_metadata(ToolMetadata const& metadata)
{
  return RegisteredToolMetadata{
      .name = std::string(metadata.name),
      .description = std::string(metadata.description),
      .schema_json = std::string(metadata.schema_json),
      .permission_category = std::string(metadata.permission_category),
      .output_bound_summary = std::string(metadata.output_bound_summary),
      .execution_mode = std::string(metadata.execution_mode),
      .event_rendering_hint = std::string(metadata.event_rendering_hint),
      .description_family = metadata.description_family
                                ? std::optional<std::string>(std::string(*metadata.description_family))
                                : std::nullopt};
}

ava::core::VoidResult ToolRegistry::register_tool(RegisteredTool tool)
{
  if (tool.metadata.name.empty()) {
    return std::unexpected(
        ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "tool registry entry requires a name"));
  }
  if (tool.metadata.schema_json.empty()) {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "tool registry entry requires a schema");
    error.with_context("tool", tool.metadata.name);
    return std::unexpected(std::move(error));
  }
  if (!tool.executor) {
    auto error =
        ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "tool registry entry requires an executor");
    error.with_context("tool", tool.metadata.name);
    return std::unexpected(std::move(error));
  }
  if (tool.source != ToolSource::Builtin && !tool.brokered_external) {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument,
                                  "external tool registration requires an AVA-owned broker path");
    error.with_context("tool", tool.metadata.name);
    error.with_context("source", std::string(to_string(tool.source)));
    error.with_context("source_id", tool.source_id);
    return std::unexpected(std::move(error));
  }
  auto const schema_name = tool_name_from_schema(tool.metadata.schema_json);
  if (!schema_name || *schema_name != tool.metadata.name) {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument,
                                  "tool registry schema name must match metadata name");
    error.with_context("tool", tool.metadata.name);
    if (schema_name) error.with_context("schema_name", *schema_name);
    return std::unexpected(std::move(error));
  }
  if (find(tool.metadata.name) != nullptr) {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "tool registry entry has duplicate name");
    error.with_context("tool", tool.metadata.name);
    error.with_context("source", std::string(to_string(tool.source)));
    error.with_context("source_id", tool.source_id);
    return std::unexpected(std::move(error));
  }

  tools_.push_back(tool);
  return {};
}

RegisteredTool const* ToolRegistry::find(std::string_view name) const noexcept
{
  auto const it =
      std::ranges::find_if(tools_, [name](RegisteredTool const& tool) { return tool.metadata.name == name; });
  if (it == tools_.end()) return nullptr;
  return &*it;
}

std::span<RegisteredTool const> ToolRegistry::entries() const noexcept
{
  return std::span<RegisteredTool const>(tools_.data(), tools_.size());
}

std::vector<ToolMetadata> ToolRegistry::metadata() const
{
  std::vector<ToolMetadata> result;
  result.reserve(tools_.size());
  for (auto const& tool : tools_) result.push_back(tool.metadata.view());
  return result;
}

std::vector<std::string> ToolRegistry::tool_schemas_json(ava::tools::ToolContext const& context) const
{
  std::vector<std::string> schemas;
  schemas.reserve(tools_.size());
  for (auto const& tool : tools_) {
    if (tool.requires_lsp_diagnostics && context.lsp_diagnostics_provider == nullptr) continue;
    schemas.emplace_back(tool.metadata.schema_json);
  }
  return schemas;
}

}  // namespace ava::agent
