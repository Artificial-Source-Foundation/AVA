#include "sys.h"
#include "ava/provider/openai_stream_parser.h"
#include "ava/provider/openai_stream_parser_internal.h"
#include "ava/provider/provider_utils.h"
#include "ava/core/json.h"

#include <string_view>
#include <vector>

namespace ava::provider {
namespace {

bool is_ignored_lifecycle_event(std::string_view type)
{
  return type == "response.created" || type == "response.in_progress" || type == "response.content_part.added" || type == "response.content_part.done";
}

}  // namespace

using namespace openai_stream_parser_internal;

void OpenAIStreamParser::append_event_for_data(std::vector<StreamEvent>& events, std::string_view data)
{
  // The framing marker precedes JSON shape checks and duplicate markers remain
  // harmless, while every other post-terminal record is rejected below.
  if (data == "[DONE]" && handle_terminal_event(events, data, {}) == EventHandling::Handled)
    return;
  if (done_seen_)
  {
    append_stream_error(events, error_seen_, "OpenAI response emitted an event after its terminal marker");
    return;
  }
  if (!is_json_object_shape(data))
  {
    append_finish_reasoning_if_open(events);
    append_stream_error(events, error_seen_, "malformed OpenAI SSE event");
    return;
  }

  auto const type = ava::core::json::string_field(data, "type").value_or("");

  // Keep this order aligned with the historical discriminator chain. Family
  // handlers do not mutate parser state when returning Unhandled.
  if (handle_documented_output_item_lifecycle(events, data, type) == EventHandling::Handled)
    return;
  if (handle_reasoning_event(events, data, type) == EventHandling::Handled)
    return;
  if (handle_function_call_event(events, data, type, EventRoutingPhase::BeforeIgnoredLifecycle) == EventHandling::Handled)
    return;
  if (handle_message_event(events, data, type, EventRoutingPhase::BeforeIgnoredLifecycle) == EventHandling::Handled)
    return;
  if (is_ignored_lifecycle_event(type))
    return;
  if (handle_message_event(events, data, type, EventRoutingPhase::AfterIgnoredLifecycle) == EventHandling::Handled)
    return;
  if (handle_function_call_event(events, data, type, EventRoutingPhase::AfterIgnoredLifecycle) == EventHandling::Handled)
    return;
  if (handle_terminal_event(events, data, type) == EventHandling::Handled)
    return;

  // OpenAI may add non-content lifecycle events without changing the assistant turn.
}

}  // namespace ava::provider
