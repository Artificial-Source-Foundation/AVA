#include "sys.h"
#include "ava/agent/todo.h"
#include "ava/agent/tool_dispatch_todo.h"
#include "ava/agent/tool_result.h"

namespace ava::agent {
namespace {

ToolDispatchResult todowrite_error(ProviderToolCall const& call, ava::core::Error const& error)
{
  return with_tool_result_payload(
      ToolDispatchResult{.call_id = call.id, .name = call.name, .success = false, .result_text = serialize_todowrite_error_json(error)});
}

}  // namespace

ToolDispatchResult todowrite_result(ava::tools::ToolContext const& /*context*/, ProviderToolCall const& call)
{
  auto parsed = parse_todowrite_arguments(call.arguments_json);
  if (!parsed)
    return todowrite_error(call, parsed.error());

  auto result_text = serialize_todowrite_success_json(*parsed);
  ToolDispatchResult result{.call_id = call.id, .name = call.name, .success = true, .result_text = std::move(result_text)};
  result.payload.summary = parsed->todos.empty() ? std::string("todos cleared")
                                                 : (std::to_string(parsed->counts.completed) + "/" + std::to_string(parsed->counts.total) + " completed");
  result.payload.content_type = "application/json";
  result.payload.status = ToolResultStatus::Success;
  return with_tool_result_payload(std::move(result));
}

}  // namespace ava::agent
