#include "sys.h"
#include "ava/provider/openai_response_parser_detail.h"
#include "ava/provider/openai_stream_parser_internal.h"
#include "ava/core/json.h"

#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ava::provider::openai_stream_parser_internal {

void append_stream_error(std::vector<StreamEvent>& events, bool& error_seen, std::string message)
{
  error_seen = true;
  events.push_back(
      StreamEvent{.type = StreamEventType::Error, .text = "", .tool_call_id = "", .tool_name = "", .error_message = std::move(message), .usage = std::nullopt});
}

ava::core::Result<std::optional<std::size_t>> documented_output_index(std::string_view data, std::string_view item)
{
  auto const parse = [](std::string_view object) -> ava::core::Result<std::optional<std::size_t>> {
    if (!ava::core::json::field_value_start(object, "output_index"))
      return std::optional<std::size_t>{};
    auto const value = ava::core::json::integer_field(object, "output_index");
    if (!value || *value < 0 || static_cast<unsigned long long>(*value) > std::numeric_limits<std::size_t>::max())
    {
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Provider, "OpenAI output item has an invalid output_index"));
    }
    return std::optional<std::size_t>{static_cast<std::size_t>(*value)};
  };
  auto const event_index = parse(data);
  if (!event_index)
    return std::unexpected(std::move(event_index.error()));
  auto const item_index = parse(item);
  if (!item_index)
    return std::unexpected(std::move(item_index.error()));
  if (*event_index && *item_index && *event_index != *item_index)
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Provider, "OpenAI output item has conflicting output_index values"));
  }
  return *event_index ? *event_index : *item_index;
}

ava::core::Result<AssistantPhase> documented_message_phase(std::string_view data, std::string_view item)
{
  auto const parse = [](std::string_view object) -> ava::core::Result<std::optional<AssistantPhase>> {
    if (!ava::core::json::field_value_start(object, "phase") || detail::is_null_field(object, "phase"))
      return std::optional<AssistantPhase>{};
    auto const value = ava::core::json::string_field(object, "phase");
    if (!value || value->empty())
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Provider, "OpenAI message output item has an empty or invalid phase"));
    auto const phase = assistant_phase_from_string(*value);
    if (!phase)
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Provider, "OpenAI message output item has an unknown phase"));
    return *phase;
  };
  auto const event_phase = parse(data);
  if (!event_phase)
    return std::unexpected(std::move(event_phase.error()));
  auto const item_phase = parse(item);
  if (!item_phase)
    return std::unexpected(std::move(item_phase.error()));
  if (*event_phase && *item_phase && *event_phase != *item_phase)
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Provider, "OpenAI message output item has conflicting phase values"));
  }
  return *event_phase ? **event_phase : (*item_phase ? **item_phase : AssistantPhase::Unknown);
}

}  // namespace ava::provider::openai_stream_parser_internal
