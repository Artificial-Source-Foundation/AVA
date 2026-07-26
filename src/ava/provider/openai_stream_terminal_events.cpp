#include "sys.h"
#include "ava/provider/openai_response_parser.h"
#include "ava/provider/openai_response_parser_detail.h"
#include "ava/provider/openai_stream_parser.h"
#include "ava/provider/openai_stream_parser_internal.h"

#include <optional>
#include <string_view>
#include <vector>

namespace ava::provider {

using namespace openai_stream_parser_internal;

OpenAIStreamParser::EventHandling OpenAIStreamParser::handle_terminal_event(std::vector<StreamEvent>& events, std::string_view data, std::string_view type)
{
  if (data == "[DONE]")
  {
    if (done_seen_)
      return EventHandling::Handled;
    done_seen_ = true;
    if (reject_unended_documented_function_calls(events) || reject_unended_documented_message_items(events))
      return EventHandling::Handled;
    append_finish_reasoning_if_open(events);
    events.push_back(StreamEvent{.type = StreamEventType::Done,
                                 .text = "",
                                 .tool_call_id = "",
                                 .tool_name = "",
                                 .error_message = "",
                                 .usage = std::nullopt,
                                 .finish_reason = saw_refusal_ ? ProviderFinishReason::Refusal : ProviderFinishReason::Completed});
    return EventHandling::Handled;
  }
  if (type == "response.completed" || type == "response.incomplete")
  {
    if (done_seen_)
      return EventHandling::Handled;
    done_seen_ = true;
    if (reject_unended_documented_function_calls(events) || reject_unended_documented_message_items(events))
      return EventHandling::Handled;
    append_finish_reasoning_if_open(events);
    events.push_back(StreamEvent{
        .type = StreamEventType::Done,
        .text = "",
        .tool_call_id = "",
        .tool_name = "",
        .error_message = "",
        .usage = parse_openai_usage(data),
        .finish_reason = saw_refusal_ ? ProviderFinishReason::Refusal
                                      : (type == "response.completed" ? ProviderFinishReason::Completed : detail::openai_response_finish_reason(data))});
    return EventHandling::Handled;
  }
  if (type == "error" || type == "response.error" || type == "response.failed")
  {
    append_finish_reasoning_if_open(events);
    append_stream_error(events, error_seen_, "OpenAI provider reported a streaming error");
    return EventHandling::Handled;
  }
  return EventHandling::Unhandled;
}

}  // namespace ava::provider
