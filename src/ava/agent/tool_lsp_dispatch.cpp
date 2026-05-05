#include "ava/agent/tool_lsp_dispatch.h"

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>

#include "ava/agent/tool_arguments.h"
#include "ava/agent/tool_dispatch_support.h"
#include "ava/agent/tool_result_json.h"
#include "ava/core/json.h"
#include "ava/tools/lsp_tools.h"

namespace ava::agent::detail {
namespace {

constexpr std::size_t kMaxLspProviderDiagnostics = 200;
constexpr std::size_t kMaxLspProviderJsonBytes = 64 * 1024;
constexpr std::size_t kMaxLspProviderPathBytes = 4096;

}  // namespace

bool is_lsp_diagnostics_metadata(ToolMetadata const& tool)
{
  return tool.name == std::string_view("lsp_diagnostics");
}

ToolDispatchResult lsp_diagnostics_result(ava::tools::ToolContext const& context, ProviderToolCall const& call)
{
  auto path = required_safe_string_arg(call.arguments_json, "path", call.name);
  if (!path) return tool_error_result(call, path.error());
  if (path->size() > kMaxLspProviderPathBytes) {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "lsp_diagnostics path is too long");
    error.with_context("max_bytes", std::to_string(kMaxLspProviderPathBytes));
    return tool_error_result(call, error);
  }
  auto const tool_context = context_for_provider_tool(context, call);
  auto result = ava::tools::lsp_diagnostics(tool_context, workspace_path(context, *path));
  if (!result) return lsp_error_result(call, result.error());

  std::string text =
      "{\"tool\":\"lsp_diagnostics\",\"ok\":true,\"path\":\"" + ava::core::json::escape(*path) + "\",\"diagnostics\":[";
  auto const total_diagnostics = result->diagnostics.size();
  bool truncated = false;
  for (std::size_t index = 0; index < result->diagnostics.size(); ++index) {
    if (index >= kMaxLspProviderDiagnostics) {
      truncated = true;
      break;
    }
    auto const& diagnostic = result->diagnostics[index];
    auto entry = std::string{"{\"severity\":"} + std::to_string(diagnostic.severity) + ",\"message\":\"" +
                 ava::core::json::escape(diagnostic.message) + "\",\"line\":" + std::to_string(diagnostic.line) +
                 ",\"column\":" + std::to_string(diagnostic.column) + ",\"code\":\"" +
                 ava::core::json::escape(diagnostic.code) + "\"}";
    if (text.size() + entry.size() + 80 > kMaxLspProviderJsonBytes) {
      truncated = true;
      break;
    }
    if (index > 0) text += ',';
    text += std::move(entry);
  }
  text += "],\"truncated\":" + json_bool_literal(truncated) +
          ",\"total_diagnostics\":" + std::to_string(total_diagnostics) + "}";
  return ToolDispatchResult{.call_id = call.id, .name = call.name, .success = true, .result_text = std::move(text)};
}

}  // namespace ava::agent::detail
