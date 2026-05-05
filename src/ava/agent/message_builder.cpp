#include "ava/agent/message_builder.h"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ava/agent/message_builder_support.h"
#include "ava/core/json.h"
#include "ava/session/session_store.h"

namespace ava::agent {
namespace {

struct NativeToolReplayPair {
  std::size_t call_index = 0;
  std::size_t result_index = 0;
  ava::provider::ContentPart tool_use;
  ava::provider::ContentPart tool_result;
};

struct NativeToolReplayBatch {
  std::size_t end_index = 0;
  std::vector<NativeToolReplayPair> pairs;
};

std::optional<std::size_t> matching_tool_result_index(std::vector<ava::session::SessionEntry> const& entries,
                                                      std::size_t index, std::string_view call_id)
{
  if (call_id.empty()) return std::nullopt;
  for (std::size_t next_index = index + 1; next_index < entries.size(); ++next_index) {
    auto const& next = entries[next_index];
    if (next.type == ava::session::EntryType::PermissionDecision) continue;
    if (next.type == ava::session::EntryType::ToolResult &&
        ava::core::json::string_field(next.data_json, "call_id").value_or("") == call_id) {
      return next_index;
    }
    return std::nullopt;
  }
  return std::nullopt;
}

bool next_entry_is_matching_tool_result(std::vector<ava::session::SessionEntry> const& entries, std::size_t index,
                                        std::string_view call_id)
{
  return matching_tool_result_index(entries, index, call_id).has_value();
}

std::optional<NativeToolReplayBatch> collect_native_tool_replay_batch(
    std::vector<ava::session::SessionEntry> const& entries, std::size_t assistant_index, std::size_t tool_call_count,
    std::vector<std::string> const& emitted_native_tool_use_ids, std::size_t max_tool_result_context_bytes)
{
  if (tool_call_count <= 1) return std::nullopt;
  NativeToolReplayBatch batch;
  std::vector<std::string> batch_tool_use_ids;
  std::size_t cursor = assistant_index + 1;
  for (std::size_t count = 0; count < tool_call_count; ++count) {
    if (cursor >= entries.size() || entries[cursor].type != ava::session::EntryType::ToolCall) return std::nullopt;
    auto const call_id = ava::core::json::string_field(entries[cursor].data_json, "call_id").value_or("");
    if (detail::contains_string(emitted_native_tool_use_ids, call_id) ||
        detail::contains_string(batch_tool_use_ids, call_id)) {
      return std::nullopt;
    }
    auto tool_use_parts = detail::tool_call_content_parts(entries[cursor]);
    if (tool_use_parts.size() != 1) return std::nullopt;
    auto const result_index = matching_tool_result_index(entries, cursor, call_id);
    if (!result_index) return std::nullopt;
    auto tool_result_parts = detail::tool_result_content_parts(entries[*result_index], max_tool_result_context_bytes);
    if (tool_result_parts.size() != 1) return std::nullopt;
    batch.pairs.push_back(NativeToolReplayPair{.call_index = cursor,
                                               .result_index = *result_index,
                                               .tool_use = std::move(tool_use_parts.front()),
                                               .tool_result = std::move(tool_result_parts.front())});
    batch_tool_use_ids.push_back(call_id);
    cursor = *result_index + 1;
  }
  batch.end_index = cursor;
  return batch;
}

std::vector<std::string> next_tool_call_ids(std::vector<ava::session::SessionEntry> const& entries,
                                            std::size_t assistant_index, std::size_t tool_call_count)
{
  std::vector<std::string> ids;
  for (std::size_t index = assistant_index + 1; index < entries.size() && ids.size() < tool_call_count; ++index) {
    auto const type = entries[index].type;
    if (type == ava::session::EntryType::UserMessage || type == ava::session::EntryType::AssistantMessage ||
        type == ava::session::EntryType::Compaction) {
      break;
    }
    if (type != ava::session::EntryType::ToolCall) continue;
    auto const id = ava::core::json::string_field(entries[index].data_json, "call_id").value_or("");
    if (!id.empty()) ids.push_back(id);
  }
  return ids;
}

}  // namespace

ava::core::Result<BuiltProviderMessages> build_messages(ava::session::SessionStore const& store,
                                                        std::size_t max_tool_result_context_bytes)
{
  auto entries = store.load();
  if (!entries) return std::unexpected(entries.error());
  bool used_compacted_context = false;
  for (auto const& entry : *entries) {
    if (entry.type == ava::session::EntryType::Compaction) used_compacted_context = true;
  }
  auto messages = build_provider_messages_from_entries(
      *entries, MessageBuildOptions{.max_tool_result_context_bytes = max_tool_result_context_bytes});
  if (!messages) return std::unexpected(messages.error());
  return BuiltProviderMessages{.messages = std::move(*messages), .used_compacted_context = used_compacted_context};
}

ava::core::Result<std::vector<ava::provider::ChatMessage>> build_provider_messages_from_entries(
    std::vector<ava::session::SessionEntry> const& entries, MessageBuildOptions options)
{
  std::vector<ava::provider::ChatMessage> messages;

  std::size_t start_index = 0;
  for (std::size_t index = 0; index < entries.size(); ++index) {
    if (entries[index].type == ava::session::EntryType::Compaction) {
      messages.clear();
      messages.push_back(
          ava::provider::ChatMessage{.role = "user", .content = detail::compaction_context_text(entries[index])});
      start_index = index + 1;
    }
  }

  std::vector<std::string> pending_native_tool_use_ids;
  std::vector<std::string> emitted_native_tool_use_ids;
  std::vector<std::string> suppressed_native_tool_use_ids;
  std::vector<ava::provider::ContentPart> pending_reasoning_parts;
  for (std::size_t index = start_index; index < entries.size(); ++index) {
    auto const& entry = entries[index];
    if (entry.type == ava::session::EntryType::UserMessage) {
      pending_reasoning_parts.clear();
      messages.push_back(ava::provider::ChatMessage{.role = "user", .content = detail::entry_text(entry)});
    } else if (entry.type == ava::session::EntryType::ReasoningBlock) {
      if (auto part = detail::reasoning_content_part(entry)) pending_reasoning_parts.push_back(std::move(*part));
    } else if (entry.type == ava::session::EntryType::AssistantMessage) {
      auto const tool_call_count = detail::assistant_tool_call_count(entry);
      if (auto const batch = collect_native_tool_replay_batch(
              entries, index, tool_call_count, emitted_native_tool_use_ids, options.max_tool_result_context_bytes)) {
        std::string assistant_content;
        detail::append_fallback_text(assistant_content, detail::entry_text(entry));
        std::vector<ava::provider::ContentPart> assistant_parts;
        detail::append_pending_reasoning_parts(assistant_parts, pending_reasoning_parts);
        if (!detail::entry_text(entry).empty()) {
          assistant_parts.push_back(ava::provider::ContentPart{.type = ava::provider::ContentPartType::Text,
                                                               .text = detail::entry_text(entry),
                                                               .tool_call_id = "",
                                                               .tool_name = "",
                                                               .input_json = "",
                                                               .is_error = false});
        }
        std::string user_content;
        std::vector<ava::provider::ContentPart> user_parts;
        for (auto const& pair : batch->pairs) {
          detail::append_fallback_text(assistant_content, detail::tool_call_context_text(entries[pair.call_index]));
          assistant_parts.push_back(pair.tool_use);
          detail::append_fallback_text(
              user_content, detail::truncate_tool_context(detail::tool_context_text(entries[pair.result_index]),
                                                          options.max_tool_result_context_bytes));
          user_parts.push_back(pair.tool_result);
          emitted_native_tool_use_ids.push_back(pair.tool_use.tool_call_id);
        }
        messages.push_back(ava::provider::ChatMessage{
            .role = "assistant", .content = std::move(assistant_content), .content_parts = std::move(assistant_parts)});
        messages.push_back(ava::provider::ChatMessage{
            .role = "user", .content = std::move(user_content), .content_parts = std::move(user_parts)});
        index = batch->end_index - 1;
        continue;
      }
      if (tool_call_count > 1) {
        for (auto id : next_tool_call_ids(entries, index, tool_call_count)) {
          suppressed_native_tool_use_ids.push_back(std::move(id));
        }
      }
      if (!pending_reasoning_parts.empty()) {
        std::vector<ava::provider::ContentPart> assistant_parts;
        detail::append_pending_reasoning_parts(assistant_parts, pending_reasoning_parts);
        if (!detail::entry_text(entry).empty()) {
          assistant_parts.push_back(ava::provider::ContentPart{.type = ava::provider::ContentPartType::Text,
                                                               .text = detail::entry_text(entry),
                                                               .tool_call_id = "",
                                                               .tool_name = "",
                                                               .input_json = "",
                                                               .is_error = false});
        }
        messages.push_back(ava::provider::ChatMessage{
            .role = "assistant", .content = detail::entry_text(entry), .content_parts = std::move(assistant_parts)});
      } else {
        messages.push_back(ava::provider::ChatMessage{.role = "assistant", .content = detail::entry_text(entry)});
      }
    } else if (entry.type == ava::session::EntryType::ToolCall) {
      auto const call_id = ava::core::json::string_field(entry.data_json, "call_id").value_or("");
      pending_native_tool_use_ids.erase(
          std::remove(pending_native_tool_use_ids.begin(), pending_native_tool_use_ids.end(), call_id),
          pending_native_tool_use_ids.end());
      auto content_parts = detail::tool_call_content_parts(entry);
      auto const suppressed_native_id = detail::contains_string(suppressed_native_tool_use_ids, call_id);
      if (suppressed_native_id) {
        static_cast<void>(detail::erase_first_string(suppressed_native_tool_use_ids, call_id));
      }
      auto const duplicate_native_id = std::find(emitted_native_tool_use_ids.begin(), emitted_native_tool_use_ids.end(),
                                                 call_id) != emitted_native_tool_use_ids.end();
      if (suppressed_native_id || duplicate_native_id || !next_entry_is_matching_tool_result(entries, index, call_id)) {
        content_parts.clear();
      }
      if (!content_parts.empty()) {
        pending_native_tool_use_ids.push_back(content_parts.front().tool_call_id);
        emitted_native_tool_use_ids.push_back(content_parts.front().tool_call_id);
      }
      messages.push_back(ava::provider::ChatMessage{.role = "assistant",
                                                    .content = detail::tool_call_context_text(entry),
                                                    .content_parts = std::move(content_parts)});
    } else if (entry.type == ava::session::EntryType::ToolResult) {
      auto const call_id = ava::core::json::string_field(entry.data_json, "call_id").value_or("");
      auto const pending_native_tool_use =
          std::find(pending_native_tool_use_ids.begin(), pending_native_tool_use_ids.end(), call_id);
      auto const matched_native_tool_use = pending_native_tool_use != pending_native_tool_use_ids.end();
      if (matched_native_tool_use) pending_native_tool_use_ids.erase(pending_native_tool_use);
      messages.push_back(ava::provider::ChatMessage{
          .role = "user",
          .content =
              detail::truncate_tool_context(detail::tool_context_text(entry), options.max_tool_result_context_bytes),
          .content_parts = matched_native_tool_use
                               ? detail::tool_result_content_parts(entry, options.max_tool_result_context_bytes)
                               : std::vector<ava::provider::ContentPart>{}});
    }
  }

  return messages;
}

}  // namespace ava::agent
