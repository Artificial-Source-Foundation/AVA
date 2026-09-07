#include "sys.h"
#include "ava/session/run_stop.h"
#include "ava/session/assistant_output.h"
#include "ava/session/logical_projection.h"
#include "ava/session/portable_sanitization.h"
#include "ava/core/json.h"

#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <variant>

namespace ava::session {
namespace {

constexpr std::string_view kPrivateReasoningOmission = "[Provider-private reasoning metadata omitted from portable export.]";

ava::core::Error projection_error(std::string message)
{
  return ava::core::Error(ava::core::ErrorCategory::Session, std::move(message));
}

ava::core::VoidResult reject_assistant_output_diagnostics(AssistantOutputProjection const& output)
{
  for (auto const& diagnostic : output.diagnostics)
  {
    if (diagnostic.severity != AssistantOutputDiagnosticSeverity::Error)
      continue;
    auto error = projection_error("cannot project malformed assistant-output session history");
    error.with_context("diagnostic_kind", std::string(to_string(diagnostic.kind)))
        .with_context("diagnostic_entry_id", diagnostic.entry_id)
        .with_context("diagnostic", diagnostic.message);
    return std::unexpected(std::move(error));
  }
  return {};
}

std::string json_string(std::string_view value)
{
  return "\"" + ava::core::json::escape(value) + "\"";
}

bool bool_field_is_true(std::string_view object, std::string_view key)
{
  auto const start = ava::core::json::field_value_start(object, key);
  return start && object.substr(*start, 4) == "true";
}

std::string safe_reasoning_text(AssistantOutputReasoning const& reasoning)
{
  return reasoning.redacted || reasoning.text.empty() ? std::string(kPrivateReasoningOmission) : reasoning.text;
}

std::string logical_reasoning_data_json(AssistantOutputReasoning const& reasoning, AssistantTurnCommit const& commit)
{
  return "{\"provider\":" + json_string(commit.provider) + ",\"model\":" + json_string(commit.model) + ",\"format\":" + json_string(reasoning.format) +
         ",\"text\":" + json_string(safe_reasoning_text(reasoning)) + ",\"redacted\":" + (reasoning.redacted ? "true" : "false") +
         ",\"private_replay_metadata_omitted\":{\"native_item_json\":" + (reasoning.native_item_json ? "true" : "false") +
         ",\"signature\":" + (reasoning.signature ? "true" : "false") + ",\"redacted_data\":" + (reasoning.redacted_data ? "true" : "false") + "}}";
}

std::string ordered_output_json(CommittedAssistantTurn const& turn)
{
  std::string output = "[";
  bool first = true;
  for (auto const& output_item : turn.items)
  {
    if (!first)
      output += ',';
    first = false;
    output += "{\"sequence\":" + std::to_string(output_item.item.sequence) + ",\"kind\":" + json_string(to_string(output_item.item.kind));
    if (auto const* text = std::get_if<AssistantOutputText>(&output_item.item.payload))
    {
      output += ",\"text\":" + json_string(text->text) + ",\"assistant_phase\":" + json_string(to_string(text->assistant_phase));
    }
    else if (auto const* reasoning = std::get_if<AssistantOutputReasoning>(&output_item.item.payload))
    {
      output +=
          ",\"text\":" + json_string(safe_reasoning_text(*reasoning)) + ",\"format\":" + json_string(reasoning->format) +
          ",\"redacted\":" + (reasoning->redacted ? "true" : "false") + ",\"native_item_json_present\":" + (reasoning->native_item_json ? "true" : "false") +
          ",\"signature_present\":" + (reasoning->signature ? "true" : "false") + ",\"redacted_data_present\":" + (reasoning->redacted_data ? "true" : "false");
    }
    else if (auto const* function = std::get_if<AssistantOutputFunctionCall>(&output_item.item.payload))
    {
      output += ",\"call_id\":" + json_string(function->call_id) + ",\"name\":" + json_string(function->name) +
                ",\"arguments\":" + json_string(function->arguments_json);
    }
    output += '}';
  }
  output += ']';
  return output;
}

std::string logical_assistant_message_data_json(CommittedAssistantTurn const& turn)
{
  std::string text;
  std::size_t tool_calls = 0;
  for (auto const& output_item : turn.items)
  {
    if (auto const* item = std::get_if<AssistantOutputText>(&output_item.item.payload))
      text += item->text;
    else if (std::holds_alternative<AssistantOutputFunctionCall>(output_item.item.payload))
      ++tool_calls;
  }

  std::string data = "{\"text\":" + json_string(text) + ",\"tool_calls\":" + std::to_string(tool_calls);
  if (turn.commit.usage_json)
    data += ",\"usage\":" + *turn.commit.usage_json;
  data += ",\"ordered_output\":" + ordered_output_json(turn) + '}';
  return data;
}

std::string ordered_text_data_json(AssistantOutputText const& text)
{
  return "{\"text\":" + json_string(text.text) + ",\"tool_calls\":0,\"assistant_phase\":" + json_string(to_string(text.assistant_phase)) + "}";
}

std::string logical_tool_call_data_json(AssistantOutputFunctionCall const& function)
{
  return "{\"call_id\":" + json_string(function.call_id) + ",\"name\":" + json_string(function.name) +
         ",\"arguments\":" + json_string(function.arguments_json) + "}";
}

SessionEntry portable_output_item_entry(SessionEntry const& entry, AssistantOutputItem const& item)
{
  auto portable = item;
  portable.provider_item_id = std::nullopt;
  portable.provider_output_index = std::nullopt;
  if (auto* reasoning = std::get_if<AssistantOutputReasoning>(&portable.payload))
  {
    reasoning->text = safe_reasoning_text(*reasoning);
    reasoning->signature = std::nullopt;
    reasoning->redacted_data = std::nullopt;
    reasoning->native_item_json = std::nullopt;
    reasoning->private_replay_metadata_omitted = true;
  }
  auto data = serialize_assistant_output_item_data_json(portable);
  if (!data)
    return SessionEntry{}; // The caller converts this impossible validated-codec failure into an error.
  auto result = entry;
  result.data_json = std::move(*data);
  return result;
}

SessionEntry portable_turn_commit_entry(SessionEntry const& entry, AssistantTurnCommit const& commit)
{
  auto data = serialize_assistant_turn_commit_data_json(commit);
  if (!data)
    return SessionEntry{};
  auto result = entry;
  result.data_json = std::move(*data);
  return result;
}

void clear_omitted_parent_references(std::vector<SessionEntry>& projected)
{
  std::unordered_set<std::string> emitted_ids;
  for (auto& entry : projected)
  {
    if (!entry.parent_id.empty() && !emitted_ids.contains(entry.parent_id))
      entry.parent_id.clear();
    emitted_ids.insert(entry.id);
  }
}

}  // namespace

ava::core::Result<std::vector<SessionEntry>> project_logical_session_history(std::vector<SessionEntry> const& entries)
{
  for (auto const& entry : entries)
    if (entry.type == EntryType::RunStop)
      if (auto stop = parse_run_stop(entry); !stop)
        return std::unexpected(std::move(stop.error()));
  try
  {
    auto const assistant_output = classify_assistant_output(entries);
    if (auto valid_output = reject_assistant_output_diagnostics(assistant_output); !valid_output)
      return std::unexpected(std::move(valid_output.error()));

    std::vector<SessionEntry> projected;
    projected.reserve(entries.size());
    for (std::size_t index = 0; index < entries.size(); ++index)
    {
      auto const& entry = entries[index];
      if (entry.type == EntryType::AssistantOutputItem)
        continue;
      if (entry.type != EntryType::AssistantTurnCommit)
      {
        auto public_entry = entry;
        if (public_entry.type == EntryType::ToolResult)
          public_entry.data_json = sanitized_tool_result_data_json(entry, false);
        if (public_entry.type == EntryType::Error)
          public_entry = sanitize_session_error_for_public_projection(std::move(public_entry));
        projected.push_back(std::move(public_entry)); // v0-v3 compatibility fields are otherwise unchanged.
        continue;
      }

      auto const* turn = assistant_output.find_turn_by_commit_index(index);
      if (!turn)
        continue; // Valid final staging suffixes are intentionally invisible.
      // Keep the prior compatibility shape: reasoning, one aggregate assistant
      // message, then tool calls. ordered_output carries the exact order.
      for (auto const& output_item : turn->items)
      {
        if (auto const* reasoning = std::get_if<AssistantOutputReasoning>(&output_item.item.payload))
        {
          auto const& source = entries[output_item.entry_index];
          projected.push_back(SessionEntry{.id = output_item.entry_id,
                                           .parent_id = source.parent_id,
                                           .type = EntryType::ReasoningBlock,
                                           .timestamp = source.timestamp,
                                           .data_json = logical_reasoning_data_json(*reasoning, turn->commit),
                                           .version = kCurrentSessionEntryVersion});
        }
      }
      projected.push_back(SessionEntry{.id = turn->commit_entry_id,
                                       .parent_id = entry.parent_id,
                                       .type = EntryType::AssistantMessage,
                                       .timestamp = entry.timestamp,
                                       .data_json = logical_assistant_message_data_json(*turn),
                                       .version = kCurrentSessionEntryVersion});
      for (auto const& output_item : turn->items)
      {
        if (auto const* function = std::get_if<AssistantOutputFunctionCall>(&output_item.item.payload))
        {
          auto const& source = entries[output_item.entry_index];
          projected.push_back(SessionEntry{.id = output_item.entry_id,
                                           .parent_id = source.parent_id,
                                           .type = EntryType::ToolCall,
                                           .timestamp = source.timestamp,
                                           .data_json = logical_tool_call_data_json(*function),
                                           .version = kCurrentSessionEntryVersion});
        }
      }
    }
    clear_omitted_parent_references(projected);
    return projected;
  }
  catch (...)
  {
    return std::unexpected(projection_error("failed to construct compatibility session projection"));
  }
}

ava::core::Result<std::vector<SessionEntry>> project_ordered_public_session_history(std::vector<SessionEntry> const& entries)
{
  for (auto const& entry : entries)
    if (entry.type == EntryType::RunStop)
      if (auto stop = parse_run_stop(entry); !stop)
        return std::unexpected(std::move(stop.error()));
  try
  {
    auto const assistant_output = classify_assistant_output(entries);
    if (auto valid_output = reject_assistant_output_diagnostics(assistant_output); !valid_output)
      return std::unexpected(std::move(valid_output.error()));

    std::vector<SessionEntry> projected;
    projected.reserve(entries.size());
    for (std::size_t index = 0; index < entries.size(); ++index)
    {
      auto const& entry = entries[index];
      if (entry.type == EntryType::AssistantOutputItem)
        continue;
      if (entry.type != EntryType::AssistantTurnCommit)
      {
        auto public_entry = entry;
        if (public_entry.type == EntryType::ToolResult)
          public_entry.data_json = sanitized_tool_result_data_json(entry, false);
        if (public_entry.type == EntryType::Error)
          public_entry = sanitize_session_error_for_public_projection(std::move(public_entry));
        projected.push_back(std::move(public_entry));
        continue;
      }

      auto const* turn = assistant_output.find_turn_by_commit_index(index);
      if (!turn)
        continue;
      for (auto const& output_item : turn->items)
      {
        auto const& source = entries[output_item.entry_index];
        if (auto const* text = std::get_if<AssistantOutputText>(&output_item.item.payload))
        {
          projected.push_back(SessionEntry{.id = output_item.entry_id,
                                           .parent_id = source.parent_id,
                                           .type = EntryType::AssistantMessage,
                                           .timestamp = source.timestamp,
                                           .data_json = ordered_text_data_json(*text),
                                           .version = kCurrentSessionEntryVersion});
        }
        else if (auto const* reasoning = std::get_if<AssistantOutputReasoning>(&output_item.item.payload))
        {
          projected.push_back(SessionEntry{.id = output_item.entry_id,
                                           .parent_id = source.parent_id,
                                           .type = EntryType::ReasoningBlock,
                                           .timestamp = source.timestamp,
                                           .data_json = logical_reasoning_data_json(*reasoning, turn->commit),
                                           .version = kCurrentSessionEntryVersion});
        }
        else if (auto const* function = std::get_if<AssistantOutputFunctionCall>(&output_item.item.payload))
        {
          projected.push_back(SessionEntry{.id = output_item.entry_id,
                                           .parent_id = source.parent_id,
                                           .type = EntryType::ToolCall,
                                           .timestamp = source.timestamp,
                                           .data_json = logical_tool_call_data_json(*function),
                                           .version = kCurrentSessionEntryVersion});
        }
      }
    }
    clear_omitted_parent_references(projected);
    return projected;
  }
  catch (...)
  {
    return std::unexpected(projection_error("failed to construct ordered public session projection"));
  }
}

std::string sanitized_compatibility_assistant_message_data_json(std::string_view data_json, bool include_ordered_output)
{
  auto const text = ava::core::json::string_field(data_json, "text").value_or("");
  std::string data = "{\"text\":" + json_string(text);
  if (auto const tool_calls = ava::core::json::integer_field(data_json, "tool_calls"); tool_calls && *tool_calls >= 0)
    data += ",\"tool_calls\":" + std::to_string(*tool_calls);
  if (auto const usage = ava::core::json::object_field(data_json, "usage"))
    data += ",\"usage\":" + *usage;

  if (!include_ordered_output || !ava::core::json::field_value_start(data_json, "ordered_output"))
    return data + '}';
  auto const output = ava::core::json::strict_objects_in_array_field(data_json, "ordered_output");
  if (!output)
    return data + '}';

  std::string ordered = "[";
  for (std::size_t index = 0; index < output->size(); ++index)
  {
    auto const& item = output->at(index);
    auto const sequence = ava::core::json::integer_field(item, "sequence");
    auto const kind = ava::core::json::string_field(item, "kind");
    if (!sequence || *sequence != static_cast<long long>(index) || !kind)
      return data + '}';
    if (index > 0)
      ordered += ',';
    ordered += "{\"sequence\":" + std::to_string(*sequence) + ",\"kind\":" + json_string(*kind);
    if (*kind == "text")
    {
      auto const item_text = ava::core::json::string_field(item, "text");
      auto const phase = ava::core::json::string_field(item, "assistant_phase");
      if (!item_text || !phase || (*phase != "unknown" && *phase != "commentary" && *phase != "final_answer"))
        return data + '}';
      ordered += ",\"text\":" + json_string(*item_text) + ",\"assistant_phase\":" + json_string(*phase);
    }
    else if (*kind == "reasoning")
    {
      auto const item_text = ava::core::json::string_field(item, "text");
      auto const format = ava::core::json::string_field(item, "format");
      if (!item_text || !format)
        return data + '}';
      ordered += ",\"text\":" + json_string(*item_text) + ",\"format\":" + json_string(*format) +
                 ",\"redacted\":" + (bool_field_is_true(item, "redacted") ? "true" : "false") +
                 ",\"native_item_json_present\":" + (bool_field_is_true(item, "native_item_json_present") ? "true" : "false") +
                 ",\"signature_present\":" + (bool_field_is_true(item, "signature_present") ? "true" : "false") +
                 ",\"redacted_data_present\":" + (bool_field_is_true(item, "redacted_data_present") ? "true" : "false");
    }
    else if (*kind == "function_call")
    {
      auto const call_id = ava::core::json::string_field(item, "call_id");
      auto const name = ava::core::json::string_field(item, "name");
      auto const arguments = ava::core::json::string_field(item, "arguments");
      if (!call_id || !name || !arguments)
        return data + '}';
      ordered += ",\"call_id\":" + json_string(*call_id) + ",\"name\":" + json_string(*name) + ",\"arguments\":" + json_string(*arguments);
    }
    else
    {
      return data + '}';
    }
    ordered += '}';
  }
  return data + ",\"ordered_output\":" + ordered + "]}";
}

ava::core::Result<std::vector<SessionEntry>> project_portable_session_history(std::vector<SessionEntry> const& entries)
{
  for (auto const& entry : entries)
    if (entry.type == EntryType::RunStop)
      if (auto stop = parse_run_stop(entry); !stop)
        return std::unexpected(std::move(stop.error()));
  try
  {
    auto const assistant_output = classify_assistant_output(entries);
    if (auto valid_output = reject_assistant_output_diagnostics(assistant_output); !valid_output)
      return std::unexpected(std::move(valid_output.error()));

    std::vector<SessionEntry> projected;
    projected.reserve(entries.size());
    for (std::size_t index = 0; index < entries.size(); ++index)
    {
      auto const& entry = entries[index];
      if (entry.type == EntryType::AssistantOutputItem)
        continue;
      if (entry.type == EntryType::AssistantTurnCommit)
      {
        auto const* turn = assistant_output.find_turn_by_commit_index(index);
        if (!turn)
          continue;
        for (auto const& output_item : turn->items)
        {
          auto portable = portable_output_item_entry(entries[output_item.entry_index], output_item.item);
          if (portable.id.empty())
            return std::unexpected(projection_error("failed to serialize a portable assistant output item"));
          projected.push_back(std::move(portable));
        }
        auto portable_commit = portable_turn_commit_entry(entry, turn->commit);
        if (portable_commit.id.empty())
          return std::unexpected(projection_error("failed to serialize a portable assistant turn commit"));
        projected.push_back(std::move(portable_commit));
        continue;
      }

      auto portable = sanitize_session_entry_for_portable_jsonl_export(entry);
      if (portable.type == EntryType::ToolResult)
        portable.data_json = sanitized_tool_result_data_json(entry, entry.version >= 4);
      projected.push_back(std::move(portable));
    }
    return projected;
  }
  catch (...)
  {
    return std::unexpected(projection_error("failed to construct portable session projection"));
  }
}

}  // namespace ava::session
