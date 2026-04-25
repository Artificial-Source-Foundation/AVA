#include "ava/types/session.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <unordered_set>
#include <utility>

#include "ava/types/time.hpp"
#include "ava/types/tool.hpp"

namespace ava::types {
namespace {

[[nodiscard]] bool is_whitespace_only(const std::string& value) {
  return value.find_first_not_of(" \t\r\n") == std::string::npos;
}

void add_stats(SessionRecoveryStats& into, const SessionRecoveryStats& from) {
  into.interrupted_tools_added += from.interrupted_tools_added;
  into.empty_assistant_removed += from.empty_assistant_removed;
  into.orphan_tool_removed += from.orphan_tool_removed;
  into.non_user_after_terminal_removed += from.non_user_after_terminal_removed;
  into.consecutive_users_merged += from.consecutive_users_merged;
  into.duplicate_messages_removed += from.duplicate_messages_removed;
}

void refresh_branch_head(SessionRecord& session) {
  if(session.branch_head.has_value()) {
    const auto found = std::any_of(session.messages.begin(), session.messages.end(), [&](const auto& message) {
      return message.id == *session.branch_head;
    });
    if(found) {
      return;
    }
  }
  if(session.messages.empty()) {
    session.branch_head = std::nullopt;
  } else {
    session.branch_head = session.messages.back().id;
  }
}

void repair_parent_links(SessionRecord& session) {
  std::unordered_set<std::string> ids;
  ids.reserve(session.messages.size());
  for(const auto& message : session.messages) {
    ids.insert(message.id);
  }

  std::optional<std::string> previous;
  for(auto& message : session.messages) {
    if(message.parent_id.has_value()
       && (*message.parent_id == message.id || !ids.contains(*message.parent_id))) {
      message.parent_id = previous == message.id ? std::nullopt : previous;
    }
    previous = message.id;
  }
}

void record_recovery_metadata(SessionRecord& session, const SessionRecoveryStats& stats) {
  if(!stats.changed()) {
    return;
  }
  session.metadata["agent"]["last_recovery"] = {
      {"at", now_utc_rfc3339()},
      {"interrupted_tools_added", stats.interrupted_tools_added},
      {"empty_assistant_removed", stats.empty_assistant_removed},
      {"orphan_tool_removed", stats.orphan_tool_removed},
      {"non_user_after_terminal_removed", stats.non_user_after_terminal_removed},
      {"consecutive_users_merged", stats.consecutive_users_merged},
      {"duplicate_messages_removed", stats.duplicate_messages_removed},
  };
}

void merge_consecutive_user_messages(SessionMessage& left, const SessionMessage& right) {
  if(left.content.empty()) {
    left.content = right.content;
  } else if(!right.content.empty()) {
    left.content += "\n\n" + right.content;
  }

  if(left.tool_calls.is_array() && right.tool_calls.is_array()) {
    for(const auto& item : right.tool_calls) {
      left.tool_calls.push_back(item);
    }
  }
  if(left.tool_results.is_array() && right.tool_results.is_array()) {
    for(const auto& item : right.tool_results) {
      left.tool_results.push_back(item);
    }
  }
  if(left.images.is_array() && right.images.is_array()) {
    for(const auto& item : right.images) {
      left.images.push_back(item);
    }
  }

  if(!left.tool_call_id.has_value()) {
    left.tool_call_id = right.tool_call_id;
  }
  left.agent_visible = left.agent_visible || right.agent_visible;
  left.user_visible = left.user_visible || right.user_visible;

  if(!left.original_content.has_value()) {
    left.original_content = right.original_content;
  } else if(right.original_content.has_value() && !right.original_content->empty()) {
    *left.original_content += "\n\n" + *right.original_content;
  }

  if(left.structured_content.is_array() && right.structured_content.is_array()) {
    for(const auto& item : right.structured_content) {
      left.structured_content.push_back(item);
    }
  } else if(!right.structured_content.empty() && !right.structured_content.is_null()) {
    left.structured_content = right.structured_content;
  }

  if(left.metadata.is_object() && right.metadata.is_object()) {
    left.metadata.update(right.metadata);
  } else if(!right.metadata.empty() && !right.metadata.is_null()) {
    left.metadata = right.metadata;
  }
}

[[nodiscard]] std::string interrupted_tool_message_id(std::size_t index, const std::string& call_id) {
  return "interrupted-tool-" + std::to_string(index) + "-" + call_id;
}

}  // namespace

SessionRecoveryStats synthesize_interrupted_tool_results(SessionRecord& session) {
  SessionRecoveryStats stats;
  std::unordered_set<std::string> answered;
  answered.reserve(session.messages.size());
  for(const auto& message : session.messages) {
    if(message.role == "tool" && message.tool_call_id.has_value()) {
      answered.insert(*message.tool_call_id);
    }
  }

  std::vector<std::string> orphaned;
  for(const auto& message : session.messages) {
    if(message.role != "assistant" || !message.tool_calls.is_array()) {
      continue;
    }
    for(const auto& item : message.tool_calls) {
      if(!item.is_object()) {
        continue;
      }
      const auto call_id = item.value("id", std::string{});
      if(!call_id.empty() && !answered.contains(call_id)) {
        orphaned.push_back(call_id);
      }
    }
  }

  if(orphaned.empty()) {
    return stats;
  }

  auto parent = session.branch_head.has_value() ? session.branch_head : std::optional<std::string>{};
  if(!parent.has_value() && !session.messages.empty()) {
    parent = session.messages.back().id;
  }
  const auto timestamp = now_utc_rfc3339();
  for(std::size_t index = 0; index < orphaned.size(); ++index) {
    const auto& call_id = orphaned[index];
    ToolResult result{
        .call_id = call_id,
        .content = "[Tool execution was interrupted]",
        .is_error = true,
    };
    nlohmann::json result_json = result;
    SessionMessage message{
        .id = interrupted_tool_message_id(index, call_id),
        .role = "tool",
        .content = result.content,
        .tool_results = nlohmann::json::array({result_json}),
        .tool_call_id = call_id,
        .timestamp = timestamp,
        .parent_id = parent,
    };
    parent = message.id;
    session.messages.push_back(std::move(message));
    ++stats.interrupted_tools_added;
  }
  session.updated_at = timestamp;
  session.branch_head = session.messages.back().id;
  return stats;
}

SessionRecoveryStats repair_session_transcript_structure(SessionRecord& session) {
  SessionRecoveryStats stats;
  auto& messages = session.messages;
  if(messages.empty()) {
    return stats;
  }

  const auto before_empty = messages.size();
  messages.erase(
      std::remove_if(messages.begin(), messages.end(), [](const SessionMessage& message) {
        return message.role == "assistant" && is_whitespace_only(message.content)
               && (!message.tool_calls.is_array() || message.tool_calls.empty());
      }),
      messages.end()
  );
  stats.empty_assistant_removed += before_empty - messages.size();

  std::unordered_set<std::string> valid_tool_calls;
  for(const auto& message : messages) {
    if(message.role != "assistant" || !message.tool_calls.is_array()) {
      continue;
    }
    for(const auto& item : message.tool_calls) {
      if(item.is_object()) {
        const auto call_id = item.value("id", std::string{});
        if(!call_id.empty()) {
          valid_tool_calls.insert(call_id);
        }
      }
    }
  }

  const auto before_orphan = messages.size();
  messages.erase(
      std::remove_if(messages.begin(), messages.end(), [&valid_tool_calls](const SessionMessage& message) {
        if(message.role != "tool") {
          return false;
        }
        if(!message.tool_call_id.has_value()) {
          return true;
        }
        return !valid_tool_calls.contains(*message.tool_call_id);
      }),
      messages.end()
  );
  stats.orphan_tool_removed += before_orphan - messages.size();

  std::optional<std::size_t> terminal_assistant_idx;
  for(std::size_t index = 0; index < messages.size(); ++index) {
    const auto& message = messages[index];
    if(message.role == "assistant" && message.tool_calls.is_array() && !message.tool_calls.empty()) {
      terminal_assistant_idx = std::nullopt;
    } else if(message.role == "assistant" && (!message.tool_calls.is_array() || message.tool_calls.empty())
       && !is_whitespace_only(message.content)) {
      terminal_assistant_idx = index;
    } else if(message.role == "user") {
      terminal_assistant_idx = std::nullopt;
    }
  }
  if(terminal_assistant_idx.has_value()) {
    auto index = *terminal_assistant_idx + 1;
    while(index < messages.size()) {
      if(messages[index].role == "tool" && messages[index].tool_call_id.has_value()
         && valid_tool_calls.contains(*messages[index].tool_call_id)) {
        ++index;
        continue;
      }
      if(messages[index].role != "user") {
        messages.erase(messages.begin() + static_cast<std::ptrdiff_t>(index));
        ++stats.non_user_after_terminal_removed;
      } else {
        ++index;
      }
    }
  }

  for(std::size_t index = 0; index + 1 < messages.size();) {
    if(messages[index].role == "user" && messages[index + 1].role == "user") {
      merge_consecutive_user_messages(messages[index], messages[index + 1]);
      messages.erase(messages.begin() + static_cast<std::ptrdiff_t>(index + 1));
      ++stats.consecutive_users_merged;
    } else {
      ++index;
    }
  }

  for(std::size_t index = 0; index + 1 < messages.size();) {
    if(messages[index].id == messages[index + 1].id && messages[index].role == messages[index + 1].role && messages[index].content == messages[index + 1].content
       && messages[index].tool_calls == messages[index + 1].tool_calls
       && messages[index].tool_results == messages[index + 1].tool_results && messages[index].images == messages[index + 1].images) {
      messages.erase(messages.begin() + static_cast<std::ptrdiff_t>(index + 1));
      ++stats.duplicate_messages_removed;
    } else {
      ++index;
    }
  }

  if(stats.changed()) {
    session.updated_at = now_utc_rfc3339();
    repair_parent_links(session);
    refresh_branch_head(session);
  }
  return stats;
}

SessionRecoveryStats recover_session_messages(SessionRecord& session) {
  SessionRecoveryStats total;
  add_stats(total, synthesize_interrupted_tool_results(session));
  add_stats(total, repair_session_transcript_structure(session));
  if(total.changed()) {
    repair_parent_links(session);
    refresh_branch_head(session);
    record_recovery_metadata(session, total);
  }
  return total;
}

}  // namespace ava::types
