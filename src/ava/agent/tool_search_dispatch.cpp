#include "ava/agent/tool_search_dispatch.h"

#include <cstddef>
#include <string>

#include "ava/agent/tool_arguments.h"
#include "ava/agent/tool_dispatch_support.h"
#include "ava/agent/tool_result_json.h"
#include "ava/core/json.h"
#include "ava/tools/search_tools.h"

namespace ava::agent::detail {

ToolDispatchResult glob_result(ava::tools::ToolContext const& context, ProviderToolCall const& call)
{
  auto pattern = required_safe_string_arg(call.arguments_json, "pattern", call.name);
  if (!pattern) return tool_error_result(call, pattern.error());
  if (auto no_ignore_allowed = reject_provider_no_ignore(call.arguments_json, call.name); !no_ignore_allowed) {
    return tool_error_result(call, no_ignore_allowed.error());
  }
  auto const tool_context = context_for_provider_tool(context, call);
  auto result = ava::tools::glob_files(
      tool_context, *pattern,
      ava::tools::GlobOptions{.max_results = optional_size_arg(call.arguments_json, "max_results", 2000, 10000)});
  if (!result) return tool_error_result(call, result.error());
  std::string text =
      "{\"tool\":\"glob\",\"ok\":true,\"pattern\":\"" + ava::core::json::escape(*pattern) + "\",\"paths\":[";
  for (std::size_t index = 0; index < result->paths.size(); ++index) {
    if (index > 0) text += ',';
    text += "\"" + ava::core::json::escape(result->paths[index].generic_string()) + "\"";
  }
  text += "],\"truncated\":" + json_bool_literal(result->truncated) +
          ",\"total_matches\":" + std::to_string(result->total_matches);
  append_tool_result_spill_fields(text, result->spill_path, result->spill_truncated);
  text += "}";
  return ToolDispatchResult{.call_id = call.id, .name = call.name, .success = true, .result_text = text};
}

ToolDispatchResult grep_result(ava::tools::ToolContext const& context, ProviderToolCall const& call)
{
  auto pattern = required_text_arg(call.arguments_json, "pattern", call.name);
  if (!pattern) return tool_error_result(call, pattern.error());
  auto const include_value = ava::core::json::string_field(call.arguments_json, "include");
  if (include_value) {
    if (auto safe = reject_control_arg(*include_value, "include", call.name); !safe) {
      return tool_error_result(call, safe.error());
    }
  }
  auto const include = include_value.value_or("**/*");
  if (auto no_ignore_allowed = reject_provider_no_ignore(call.arguments_json, call.name); !no_ignore_allowed) {
    return tool_error_result(call, no_ignore_allowed.error());
  }
  auto const tool_context = context_for_provider_tool(context, call);
  auto result = ava::tools::grep_files(
      tool_context, *pattern, include,
      ava::tools::GrepOptions{.max_matches = optional_size_arg(call.arguments_json, "max_matches", 2000, 10000)});
  if (!result) return tool_error_result(call, result.error());
  std::string text = "{\"tool\":\"grep\",\"ok\":true,\"pattern\":\"" + ava::core::json::escape(*pattern) +
                     "\",\"include\":\"" + ava::core::json::escape(include) + "\",\"matches\":[";
  for (std::size_t index = 0; index < result->matches.size(); ++index) {
    auto const& match = result->matches[index];
    if (index > 0) text += ',';
    text += "{\"path\":\"" + ava::core::json::escape(match.path.generic_string()) +
            "\",\"line_number\":" + std::to_string(match.line_number) + ",\"line\":\"" +
            ava::core::json::escape(match.line) + "\",\"line_truncated\":" + json_bool_literal(match.line_truncated) +
            "}";
  }
  text += "],\"truncated\":" + json_bool_literal(result->truncated) +
          ",\"total_matches\":" + std::to_string(result->total_matches);
  append_tool_result_spill_fields(text, result->spill_path, result->spill_truncated);
  text += "}";
  return ToolDispatchResult{.call_id = call.id, .name = call.name, .success = true, .result_text = text};
}

}  // namespace ava::agent::detail
