#include "sys.h"
#include "ava/provider/openai_reasoning.h"
#include "ava/provider/openai_stream_parser.h"
#include "ava/provider/openai_stream_parser_internal.h"
#include "ava/core/json.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ava::provider {
namespace {

struct DocumentedOutputItemMetadata
{
  std::string id;
  std::optional<std::size_t> output_index = std::nullopt;
  AssistantPhase phase = AssistantPhase::Unknown;
};

ava::core::VoidResult documented_output_item_metadata(std::string_view data, std::string_view item, bool is_message, std::string& id,
                                                      std::optional<std::size_t>& output_index, AssistantPhase& phase)
{
  auto const parsed_id = ava::core::json::string_field(item, "id");
  if (!parsed_id || !is_valid_openai_opaque_id(*parsed_id))
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Provider, "OpenAI output item requires a bounded item ID"));
  }
  auto parsed_output_index = openai_stream_parser_internal::documented_output_index(data, item);
  if (!parsed_output_index)
    return std::unexpected(std::move(parsed_output_index.error()));
  AssistantPhase parsed_phase = AssistantPhase::Unknown;
  if (is_message)
  {
    auto message_phase = openai_stream_parser_internal::documented_message_phase(data, item);
    if (!message_phase)
      return std::unexpected(std::move(message_phase.error()));
    parsed_phase = *message_phase;
  }
  id = *parsed_id;
  output_index = *parsed_output_index;
  phase = parsed_phase;
  return {};
}

}  // namespace

using namespace openai_stream_parser_internal;

OpenAIStreamParser::EventHandling OpenAIStreamParser::handle_documented_output_item_lifecycle(std::vector<StreamEvent>& events, std::string_view data,
                                                                                              std::string_view type)
{
  OutputItemLifecycle lifecycle;
  if (type == "response.output_item.added")
    lifecycle = OutputItemLifecycle::Added;
  else if (type == "response.output_item.done")
    lifecycle = OutputItemLifecycle::Done;
  else
    return EventHandling::Unhandled;

  auto const item = ava::core::json::object_field(data, "item");
  auto const item_type = item ? ava::core::json::string_field(*item, "type").value_or("") : "";
  if (item_type != "message" && item_type != "reasoning" && item_type != "function_call")
  {
    append_stream_error(events, error_seen_, "OpenAI response contains an unsupported output item type");
    return EventHandling::Handled;
  }

  DocumentedOutputItemMetadata metadata;
  auto parsed_metadata = documented_output_item_metadata(data, *item, item_type == "message", metadata.id, metadata.output_index, metadata.phase);
  if (!parsed_metadata)
  {
    append_stream_error(events, error_seen_, parsed_metadata.error().message());
    return EventHandling::Handled;
  }

  auto const existing = documented_output_item_types_.find(metadata.id);
  if (existing != documented_output_item_types_.end())
  {
    if (existing->second != item_type)
    {
      append_stream_error(events, error_seen_, "OpenAI output item ID changed its item type");
      return EventHandling::Handled;
    }
    if (lifecycle == OutputItemLifecycle::Added)
    {
      append_stream_error(events, error_seen_, "OpenAI output item was added more than once");
      return EventHandling::Handled;
    }
  }
  else
  {
    documented_output_item_types_.emplace(metadata.id, item_type);
  }
  if (lifecycle == OutputItemLifecycle::Added && !documented_output_item_added_ids_.insert(metadata.id).second)
  {
    append_stream_error(events, error_seen_, "OpenAI output item was added more than once");
    return EventHandling::Handled;
  }
  if (metadata.output_index)
  {
    auto const index = documented_output_item_ids_by_index_.find(*metadata.output_index);
    if (index != documented_output_item_ids_by_index_.end() && index->second != metadata.id)
    {
      append_stream_error(events, error_seen_, "OpenAI output item index is already bound to another item ID");
      return EventHandling::Handled;
    }
    documented_output_item_ids_by_index_.emplace(*metadata.output_index, metadata.id);
  }

  saw_content_ = true;
  if (item_type == "message")
    handle_documented_message_output_item(events, *item, metadata.id, metadata.output_index, metadata.phase, lifecycle);
  else if (item_type == "reasoning")
    handle_documented_reasoning_output_item(events, *item, metadata.id, metadata.output_index, lifecycle);
  else
    handle_documented_function_call_output_item(events, data, *item, metadata.output_index, lifecycle);
  return EventHandling::Handled;
}

}  // namespace ava::provider
