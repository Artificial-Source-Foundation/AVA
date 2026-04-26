#include "runtime_session_ops.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

#include "ava/types/time.hpp"

namespace ava::agent::runtime_detail {
namespace {

constexpr std::string_view kRuntimeMessageIdPrefix{"ava_msg_"};
constexpr std::array<std::string_view, 2> kGeneratedMessageIdPrefixes{
    kRuntimeMessageIdPrefix,
    // Legacy C++ sessions used this prefix before runtime message IDs were made milestone-neutral.
    "m7_msg_",
};

[[nodiscard]] std::string make_message_id(const std::string& session_id, std::uint64_t index) {
  return std::string{kRuntimeMessageIdPrefix} + std::to_string(index) + "_" + session_id;
}

[[nodiscard]] std::string normalize_role(const std::string& role) {
  std::string out = role;
  std::transform(out.begin(), out.end(), out.begin(), [](const auto ch) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  });
  return out;
}

[[nodiscard]] bool session_contains_message(const ava::types::SessionRecord& session, const std::string& id) {
  return std::any_of(session.messages.begin(), session.messages.end(), [&](const auto& message) {
    return message.id == id;
  });
}

[[nodiscard]] std::optional<std::string> append_parent_id(const ava::types::SessionRecord& session) {
  if(session.branch_head.has_value() && session_contains_message(session, *session.branch_head)) {
    return session.branch_head;
  }
  if(session.messages.empty()) {
    return std::nullopt;
  }
  return session.messages.back().id;
}

[[nodiscard]] std::vector<const ava::types::SessionMessage*> active_branch_messages(const ava::types::SessionRecord& session) {
  if(!session.branch_head.has_value()) {
    std::vector<const ava::types::SessionMessage*> all;
    all.reserve(session.messages.size());
    for(const auto& message : session.messages) {
      all.push_back(&message);
    }
    return all;
  }

  std::unordered_map<std::string, const ava::types::SessionMessage*> by_id;
  by_id.reserve(session.messages.size());
  for(const auto& message : session.messages) {
    by_id.emplace(message.id, &message);
  }

  auto current = session.branch_head;
  std::unordered_set<std::string> visited;
  std::vector<const ava::types::SessionMessage*> branch;
  while(current.has_value()) {
    if(!visited.insert(*current).second) {
      throw std::runtime_error("cycle detected in active session branch: " + *current);
    }
    const auto it = by_id.find(*current);
    if(it == by_id.end()) {
      branch.clear();
      break;
    }
    branch.push_back(it->second);
    current = it->second->parent_id;
  }

  if(branch.empty()) {
    branch.reserve(session.messages.size());
    for(const auto& message : session.messages) {
      branch.push_back(&message);
    }
    return branch;
  }

  std::reverse(branch.begin(), branch.end());
  return branch;
}

[[nodiscard]] ava::llm::ChatMessage to_chat_message(const ava::types::SessionMessage& message) {
  const auto role = normalize_role(message.role);
  if(role == "system") {
    return ava::llm::ChatMessage::system(message.content);
  }
  if(role == "assistant") {
    auto chat = ava::llm::ChatMessage::assistant(message.content);
    if(message.tool_calls.is_array()) {
      for(const auto& item : message.tool_calls) {
        if(!item.is_object()) {
          continue;
        }
        chat.tool_calls.push_back(ava::types::ToolCall{
            .id = item.value("id", std::string{}),
            .name = item.value("name", std::string{}),
            .arguments = item.value("arguments", nlohmann::json::object()),
        });
      }
    }
    return chat;
  }
  if(role == "tool") {
    try {
      const auto parsed = nlohmann::json::parse(message.content);
      if(parsed.is_object() && parsed.contains("content")) {
        auto chat = ava::llm::ChatMessage::tool(
            parsed.value("content", std::string{}),
            parsed.value("call_id", std::string{"unknown_call"})
        );
        if(message.tool_results.is_array() && !message.tool_results.empty()) {
          const auto& stored = message.tool_results.front();
          if(stored.is_object()) {
            chat.tool_call_id = stored.value("call_id", chat.tool_call_id.value_or("unknown_call"));
          }
        }
        return chat;
      }
    } catch(const std::exception&) {
      // Fall through to content-only tool fallback.
    }
    return ava::llm::ChatMessage::tool(message.content, message.tool_call_id.value_or("unknown_call"));
  }

  return ava::llm::ChatMessage::user(message.content);
}

[[nodiscard]] std::size_t estimate_message_tokens(
    const ava::types::SessionMessage& message,
    const ava::llm::Provider& provider
) {
  std::size_t total = provider.estimate_tokens(message.content);
  if(!message.tool_calls.empty()) {
    total += provider.estimate_tokens(message.tool_calls.dump());
  }
  if(!message.tool_results.empty()) {
    total += provider.estimate_tokens(message.tool_results.dump());
  }
  if(message.structured_content.is_array() && !message.structured_content.empty()) {
    total += provider.estimate_tokens(message.structured_content.dump());
  }
  return total;
}

[[nodiscard]] std::size_t estimate_chat_messages_tokens(
    const std::vector<ava::llm::ChatMessage>& messages,
    const ava::llm::Provider& provider
) {
  std::size_t total = 0;
  for(const auto& message : messages) {
    total += provider.estimate_tokens(message.content);
    if(!message.tool_calls.empty()) {
      total += provider.estimate_tokens(nlohmann::json(message.tool_calls).dump());
    }
  }
  return total;
}

[[nodiscard]] std::size_t estimate_assistant_output_tokens(
    const std::string& assistant_text,
    const std::vector<ava::types::ToolCall>& tool_calls,
    const ava::llm::Provider& provider
) {
  auto total = provider.estimate_tokens(assistant_text);
  if(!tool_calls.empty()) {
    total += provider.estimate_tokens(nlohmann::json(tool_calls).dump());
  }
  return total;
}

}  // namespace

std::uint64_t derive_message_id_counter(const ava::types::SessionRecord& session) {
  std::uint64_t max_seen = 0;
  bool saw_generated_id = false;

  for(const auto& message : session.messages) {
    std::optional<std::string_view> suffix;
    for(const auto prefix : kGeneratedMessageIdPrefixes) {
      if(message.id.starts_with(prefix)) {
        suffix = std::string_view{message.id}.substr(prefix.size());
        break;
      }
    }
    if(!suffix.has_value() || suffix->empty()) {
      continue;
    }

    std::uint64_t parsed = 0;
    const auto* begin = suffix->data();
    const auto* end = begin + suffix->size();
    const auto [parsed_end, error] = std::from_chars(begin, end, parsed);
    if(error == std::errc{} && (parsed_end == end || *parsed_end == '_')) {
      saw_generated_id = true;
      max_seen = std::max(max_seen, parsed);
    }
  }

  return saw_generated_id ? max_seen : static_cast<std::uint64_t>(session.messages.size());
}

void append_session_message(
    ava::types::SessionRecord& session,
    std::uint64_t& id_counter,
    const std::string& role,
    std::string content,
    nlohmann::json tool_calls,
    nlohmann::json tool_results,
    std::optional<std::string> tool_call_id
) {
  const auto now = ava::types::now_utc_rfc3339();
  const auto parent = append_parent_id(session);
  session.messages.push_back(ava::types::SessionMessage{
      .id = make_message_id(session.id, ++id_counter),
      .role = role,
      .content = std::move(content),
      .tool_calls = tool_calls.is_array() ? std::move(tool_calls) : nlohmann::json::array(),
      .tool_results = tool_results.is_array() ? std::move(tool_results) : nlohmann::json::array(),
      .tool_call_id = std::move(tool_call_id),
      .timestamp = now,
      .parent_id = parent,
  });
  session.updated_at = now;
  session.branch_head = session.messages.back().id;
}

std::vector<ava::llm::ChatMessage> build_chat_messages(
    const ava::types::SessionRecord& session,
    const std::string& system_prompt
) {
  std::vector<ava::llm::ChatMessage> messages;
  messages.reserve(session.messages.size() + 1);
  messages.push_back(ava::llm::ChatMessage::system(system_prompt));
  for(const auto* message : active_branch_messages(session)) {
    if(!message->agent_visible) {
      continue;
    }
    messages.push_back(to_chat_message(*message));
  }
  return messages;
}

std::size_t estimate_session_tokens(
    const ava::types::SessionRecord& session,
    const ava::llm::Provider& provider
) {
  std::size_t total = 0;
  for(const auto* message : active_branch_messages(session)) {
    if(!message->agent_visible) {
      continue;
    }
    total += estimate_message_tokens(*message, provider);
  }
  return total;
}

ava::types::TokenUsage estimate_turn_usage(
    const std::vector<ava::llm::ChatMessage>& chat_messages,
    const std::string& assistant_text,
    const std::vector<ava::types::ToolCall>& tool_calls,
    const ava::llm::Provider& provider
) {
  return ava::types::TokenUsage{
      .input_tokens = estimate_chat_messages_tokens(chat_messages, provider),
      .output_tokens = estimate_assistant_output_tokens(assistant_text, tool_calls, provider),
  };
}

std::pair<std::size_t, std::size_t> compact_session_for_agent(
    ava::types::SessionRecord& session,
    const ava::llm::Provider& provider,
    std::size_t preserve_recent_messages
) {
  auto branch = active_branch_messages(session);
  std::vector<std::string> compact_ids;
  if(branch.size() <= preserve_recent_messages) {
    return {0, 0};
  }

  auto preserve_from = branch.size() - preserve_recent_messages;
  while(preserve_from > 0) {
    const auto* first_preserved = branch[preserve_from];
    if(first_preserved == nullptr || normalize_role(first_preserved->role) != "tool") {
      break;
    }
    --preserve_from;
  }

  std::size_t token_estimate = 0;
  for(std::size_t index = 0; index < preserve_from; ++index) {
    const auto* message = branch[index];
    if(message == nullptr || !message->agent_visible) {
      continue;
    }
    compact_ids.push_back(message->id);
    token_estimate += estimate_message_tokens(*message, provider);
  }

  if(compact_ids.empty()) {
    return {0, 0};
  }

  const std::unordered_set<std::string> compact_set(compact_ids.begin(), compact_ids.end());
  for(auto& message : session.messages) {
    if(compact_set.contains(message.id)) {
      message.agent_visible = false;
      message.metadata["compaction_kind"] = "runtime_structural_compaction";
    }
  }
  session.metadata["agent"]["last_compaction"]["message_count"] = compact_ids.size();
  session.metadata["agent"]["last_compaction"]["estimated_tokens"] = token_estimate;
  session.metadata["agent"]["last_compaction"]["at"] = ava::types::now_utc_rfc3339();
  return {compact_ids.size(), token_estimate};
}

std::string encode_tool_result_message(const ava::types::ToolResult& result) {
  return nlohmann::json{{"call_id", result.call_id}, {"content", result.content}, {"is_error", result.is_error}}.dump();
}

void emit_event(const AgentEventSink& sink, AgentEvent event) {
  if(!sink) {
    return;
  }
  try {
    sink(event);
  } catch(const std::exception& ex) {
    std::cerr << "[ava-agent] event sink failed for kind=" << static_cast<int>(event.kind);
    if(event.run_id.has_value()) {
      std::cerr << " run=" << *event.run_id;
    }
    std::cerr << ": " << ex.what() << '\n';
  } catch(...) {
    std::cerr << "[ava-agent] event sink failed for kind=" << static_cast<int>(event.kind);
    if(event.run_id.has_value()) {
      std::cerr << " run=" << *event.run_id;
    }
    std::cerr << ": unknown exception\n";
  }
}

void record_unprocessed_queue_items(ava::agent::MessageQueue* queue, BudgetTracker& budget) {
  if(queue == nullptr) {
    return;
  }
  auto [steering, follow_ups, post_complete] = queue->pending_count();
  budget.record_skipped_steering(steering);
  budget.record_skipped_follow_ups(follow_ups);
  budget.record_skipped_post_complete(post_complete);
}

void persist_completion_metadata(
    ava::types::SessionRecord& session,
    AgentCompletionReason reason,
    std::size_t turns_used,
    const BudgetTracker& budget
) {
  session.metadata["agent"]["last_completion_reason"] = completion_reason_to_string(reason);
  session.metadata["agent"]["last_turns_used"] = turns_used;
  session.metadata["agent"]["budget"] = budget.to_session_metadata();
}

void persist_error_metadata(
    ava::types::SessionRecord& session,
    std::size_t turns_used,
    std::string_view error_message
) {
  session.metadata["agent"]["last_completion_reason"] = completion_reason_to_string(AgentCompletionReason::Error);
  session.metadata["agent"]["last_turns_used"] = turns_used;
  session.metadata["agent"]["last_error"] = error_message;
}

bool append_promoted_queue_messages(
    ava::types::SessionRecord& session,
    ava::agent::MessageQueue* queue,
    std::uint64_t& id_counter,
    std::size_t turn,
    const std::function<void(std::size_t, std::string)>& emit_checkpoint
) {
  if(queue == nullptr) {
    return false;
  }
  if(queue->has_follow_up()) {
    for(auto& message : queue->drain_follow_up()) {
      append_session_message(session, id_counter, "user", "[User follow-up]\n" + message);
      emit_checkpoint(turn, "checkpoint after queued follow-up");
    }
    return true;
  }
  if(queue->has_post_complete()) {
    auto [group, messages] = queue->next_post_complete_group();
    std::ostringstream combined;
    combined << "[User post-complete group " << group << "]";
    for(const auto& message : messages) {
      combined << "\n" << message;
    }
    append_session_message(session, id_counter, "user", combined.str());
    queue->finish_and_advance_post_group();
    emit_checkpoint(turn, "checkpoint after queued post-complete group");
    return true;
  }
  return false;
}

}  // namespace ava::agent::runtime_detail
