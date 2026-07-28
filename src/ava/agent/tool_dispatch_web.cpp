#include "sys.h"
#include "ava/agent/tool_dispatch_common.h"
#include "ava/agent/tool_dispatch_web.h"
#include "ava/tools/webfetch_tool.h"
#include "ava/tools/websearch_tool.h"
#include "ava/core/json.h"

namespace ava::agent {

ToolDispatchResult webfetch_result(ava::tools::ToolContext const& context, ProviderToolCall const& call)
{
  auto url = tool_dispatch::required_safe_string_arg(call.arguments_json, "url", call.name);
  if (!url)
    return tool_dispatch::tool_error_result(call, url.error());
  auto const format_text = ava::core::json::string_field(call.arguments_json, "format").value_or("markdown");
  ava::tools::WebFetchFormat format = ava::tools::WebFetchFormat::Markdown;
  if (format_text == "markdown")
  {
    format = ava::tools::WebFetchFormat::Markdown;
  }
  else if (format_text == "text")
  {
    format = ava::tools::WebFetchFormat::Text;
  }
  else if (format_text == "html")
  {
    format = ava::tools::WebFetchFormat::Html;
  }
  else
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "webfetch format is invalid");
    error.with_context("tool", call.name);
    error.with_context("format", format_text);
    return tool_dispatch::tool_error_result(call, error);
  }
  auto const tool_context = tool_dispatch::context_for_provider_tool(context, call);
  auto result = ava::tools::webfetch(tool_context, *url,
                                     ava::tools::WebFetchOptions{
                                         .max_bytes = tool_dispatch::optional_size_arg(call.arguments_json, "max_bytes", 1024 * 1024, 5 * 1024 * 1024),
                                         .offset_line = tool_dispatch::optional_size_arg(call.arguments_json, "offset", 1, 100000000),
                                         .max_lines = tool_dispatch::optional_size_arg(call.arguments_json, "limit", 200, 100000),
                                         .timeout_ms = static_cast<int>(tool_dispatch::optional_size_arg(call.arguments_json, "timeout_ms", 30000, 120000)),
                                         .format = format,
                                     });
  if (!result)
    return tool_dispatch::tool_error_result(call, result.error());
  return ToolDispatchResult{
      .call_id = call.id,
      .name = call.name,
      .success = true,
      .result_text =
          "{\"tool\":\"webfetch\",\"ok\":true,\"url\":\"" + ava::core::json::escape(result->url) + "\",\"status_code\":" + std::to_string(result->status_code) +
          ",\"content_type\":\"" + ava::core::json::escape(result->content_type) + "\",\"content\":\"" + ava::core::json::escape(result->content) +
          "\",\"truncated\":" + tool_dispatch::json_bool(result->truncated) + ",\"byte_limited\":" + tool_dispatch::json_bool(result->byte_limited) +
          ",\"line_limited\":" + tool_dispatch::json_bool(result->line_limited) + ",\"total_bytes\":" + std::to_string(result->total_bytes) +
          ",\"output_bytes\":" + std::to_string(result->output_bytes) + ",\"output_lines\":" + std::to_string(result->output_lines) + ",\"start_line\":" +
          std::to_string(result->start_line) + ",\"end_line\":" + std::to_string(result->end_line) + ",\"total_lines\":" + std::to_string(result->total_lines) +
          [&] {
            std::string suffix;
            if (result->next_offset_line > 0)
            {
              suffix += ",\"next_offset\":" + std::to_string(result->next_offset_line);
              suffix += ",\"next_offset_line\":" + std::to_string(result->next_offset_line);
              suffix += ",\"truncation_hint\":\"Call webfetch again with offset=" + std::to_string(result->next_offset_line) + " to continue.\"";
            }
            else if (result->byte_limited)
            {
              suffix += ",\"truncation_hint\":\"Increase max_bytes to read more of this range.\"";
            }
            return suffix;
          }() +
          "}"};
}

ToolDispatchResult websearch_result(ava::tools::ToolContext const& context, ProviderToolCall const& call)
{
  auto query = tool_dispatch::required_safe_string_arg(call.arguments_json, "query", call.name);
  if (!query)
    return tool_dispatch::tool_error_result(call, query.error());
  auto context_max_chars = tool_dispatch::optional_size_arg(call.arguments_json, "context_max_chars", 0, 30000);
  if (context_max_chars == 0)
  {
    context_max_chars = tool_dispatch::optional_size_arg(call.arguments_json, "contextMaxCharacters", 10000, 30000);
  }
  auto const tool_context = tool_dispatch::context_for_provider_tool(context, call);
  auto result = ava::tools::websearch(tool_context, *query,
                                      ava::tools::WebSearchOptions{
                                          .max_results = tool_dispatch::optional_size_arg(call.arguments_json, "num_results", 8, 10),
                                          .context_max_chars = context_max_chars,
                                          .timeout_ms = static_cast<int>(tool_dispatch::optional_size_arg(call.arguments_json, "timeout_ms", 25000, 60000)),
                                      });
  if (!result)
    return tool_dispatch::tool_error_result(call, result.error());
  std::string text = "{\"tool\":\"websearch\",\"ok\":true,\"query\":\"" + ava::core::json::escape(result->query) + "\",\"engine\":\"" +
                     ava::core::json::escape(result->engine) + "\",\"results\":[";
  for (std::size_t index = 0; index < result->results.size(); ++index)
  {
    auto const& item = result->results[index];
    if (index > 0)
      text += ',';
    text += "{\"title\":\"" + ava::core::json::escape(item.title) + "\",\"url\":\"" + ava::core::json::escape(item.url) + "\",\"snippet\":\"" +
            ava::core::json::escape(item.snippet) + "\"}";
  }
  text += "],\"truncated\":" + tool_dispatch::json_bool(result->truncated) + ",\"total_results\":" + std::to_string(result->total_results) +
          ",\"output_chars\":" + std::to_string(result->output_chars) + "}";
  return ToolDispatchResult{.call_id = call.id, .name = call.name, .success = true, .result_text = std::move(text)};
}

}  // namespace ava::agent
