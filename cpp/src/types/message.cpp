#include "ava/types/message.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>

namespace ava::types {
namespace {

constexpr std::string_view kInterruptedToolMessageIdPrefix = "interrupted-tool-";
constexpr std::string_view kSyntheticInterruptedToolTimestamp = "1970-01-01T00:00:00Z";

std::string interrupted_tool_message_id(std::size_t index, std::string_view call_id) {
  return std::string{kInterruptedToolMessageIdPrefix} + std::to_string(index) + "-" + std::string{call_id};
}

void merge_consecutive_user_message(Message& left, const Message& right) {
  // Merge right into left without losing user-provided context: text and original text concatenate,
  // collections append, missing optionals inherit from the right side, visibility flags OR together,
  // JSON arrays concatenate, and JSON objects shallow-merge with right-side keys winning.
  if(left.content.empty()) {
    left.content = right.content;
  } else if(!right.content.empty()) {
    left.content += "\n\n" + right.content;
  }

  left.images.insert(left.images.end(), right.images.begin(), right.images.end());
  left.tool_calls.insert(left.tool_calls.end(), right.tool_calls.begin(), right.tool_calls.end());
  left.tool_results.insert(left.tool_results.end(), right.tool_results.begin(), right.tool_results.end());

  // Preserve existing lineage when present; otherwise carry forward the merged turn's linkage.
  if(!left.tool_call_id.has_value()) {
    left.tool_call_id = right.tool_call_id;
  }
  if(!left.parent_id.has_value()) {
    left.parent_id = right.parent_id;
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
  } else if(left.structured_content.empty() || left.structured_content.is_null()) {
    left.structured_content = right.structured_content;
  }

  if(left.metadata.is_object() && right.metadata.is_object()) {
    left.metadata.update(right.metadata);
  } else if(left.metadata.empty() || left.metadata.is_null()) {
    left.metadata = right.metadata;
  }
}

}  // namespace

std::string_view role_to_string(Role role) {
  switch(role) {
    case Role::System:
      return "system";
    case Role::User:
      return "user";
    case Role::Assistant:
      return "assistant";
    case Role::Tool:
      return "tool";
  }
  return "system";
}

std::optional<Role> role_from_string(std::string_view value) {
  static constexpr std::array<std::pair<std::string_view, Role>, 4> kMap{ {
      {"system", Role::System},
      {"user", Role::User},
      {"assistant", Role::Assistant},
      {"tool", Role::Tool},
  } };

  for(const auto& [name, role] : kMap) {
    if(name == value) {
      return role;
    }
  }
  return std::nullopt;
}

void to_json(nlohmann::json& json, const ImageContent& value) {
  json = nlohmann::json{{"data", value.data}, {"media_type", value.media_type}};
}

void from_json(const nlohmann::json& json, ImageContent& value) {
  json.at("data").get_to(value.data);
  json.at("media_type").get_to(value.media_type);
}

void to_json(nlohmann::json& json, const Message& value) {
  json = nlohmann::json{
      {"id", value.id},
      {"role", role_to_string(value.role)},
      {"content", value.content},
      {"timestamp", value.timestamp},
      {"tool_calls", value.tool_calls},
      {"tool_results", value.tool_results},
      {"images", value.images},
      {"agent_visible", value.agent_visible},
      {"user_visible", value.user_visible},
      {"structured_content", value.structured_content},
      {"metadata", value.metadata},
  };

  if(value.tool_call_id.has_value()) {
    json["tool_call_id"] = *value.tool_call_id;
  }
  if(value.parent_id.has_value()) {
    json["parent_id"] = *value.parent_id;
  }
  if(value.original_content.has_value()) {
    json["original_content"] = *value.original_content;
  }
}

void from_json(const nlohmann::json& json, Message& value) {
  json.at("id").get_to(value.id);

  const auto role = role_from_string(json.value("role", "user"));
  value.role = role.value_or(Role::User);

  value.content = json.value("content", std::string{});
  value.timestamp = json.value("timestamp", std::string{});
  value.tool_calls = {};
  if(json.contains("tool_calls")) {
    if(!json.at("tool_calls").is_array()) {
      throw std::invalid_argument("Message.tool_calls must be an array");
    }
    for(const auto& item : json.at("tool_calls")) {
      ToolCall call;
      from_json(item, call);
      value.tool_calls.push_back(std::move(call));
    }
  }

  value.tool_results = {};
  if(json.contains("tool_results")) {
    if(!json.at("tool_results").is_array()) {
      throw std::invalid_argument("Message.tool_results must be an array");
    }
    for(const auto& item : json.at("tool_results")) {
      ToolResult result;
      from_json(item, result);
      value.tool_results.push_back(std::move(result));
    }
  }
  value.tool_call_id = json.contains("tool_call_id") ? std::optional<std::string>{json.at("tool_call_id").get<std::string>()}
                                                      : std::nullopt;
  value.images = {};
  if(json.contains("images")) {
    if(!json.at("images").is_array()) {
      throw std::invalid_argument("Message.images must be an array");
    }
    for(const auto& item : json.at("images")) {
      ImageContent image;
      from_json(item, image);
      value.images.push_back(std::move(image));
    }
  }
  value.parent_id = json.contains("parent_id") ? std::optional<std::string>{json.at("parent_id").get<std::string>()}
                                                : std::nullopt;
  value.agent_visible = json.value("agent_visible", true);
  value.user_visible = json.value("user_visible", true);
  value.original_content =
      json.contains("original_content") ? std::optional<std::string>{json.at("original_content").get<std::string>()}
                                        : std::nullopt;
  value.structured_content = json.value("structured_content", nlohmann::json::array());
  value.metadata = json.value("metadata", nlohmann::json::object());
}

void repair_conversation(std::vector<Message>& messages) {
  if(messages.empty()) {
    return;
  }

  messages.erase(
      std::remove_if(messages.begin(), messages.end(), [](const Message& message) {
        return message.role == Role::Assistant && message.content.find_first_not_of(" \t\r\n") == std::string::npos
               && message.tool_calls.empty();
      }),
      messages.end()
  );

  std::unordered_set<std::string> valid_tool_calls;
  for(const auto& message : messages) {
    if(message.role != Role::Assistant) {
      continue;
    }
    for(const auto& call : message.tool_calls) {
      valid_tool_calls.insert(call.id);
    }
  }

  messages.erase(
      std::remove_if(messages.begin(), messages.end(), [&valid_tool_calls](const Message& message) {
        if(message.role != Role::Tool) {
          return false;
        }
        if(!message.tool_call_id.has_value()) {
          return true;
        }
        return !valid_tool_calls.contains(*message.tool_call_id);
      }),
      messages.end()
  );

  std::optional<std::size_t> terminal_assistant_idx;
  for(std::size_t index = 0; index < messages.size(); ++index) {
    const auto& message = messages[index];
    if(message.role == Role::Assistant && message.tool_calls.empty()
       && message.content.find_first_not_of(" \t\r\n") != std::string::npos) {
      terminal_assistant_idx = index;
    } else if(message.role == Role::User) {
      terminal_assistant_idx = std::nullopt;
    }
  }

  if(terminal_assistant_idx.has_value()) {
    auto index = *terminal_assistant_idx + 1;
    while(index < messages.size()) {
      if(messages[index].role != Role::User) {
        messages.erase(messages.begin() + static_cast<std::ptrdiff_t>(index));
      } else {
        ++index;
      }
    }
  }

  // Dedupe only on user-visible turn payload. IDs, timestamps, and metadata can differ for the
  // same repaired turn, while role/content/tool/image payload equality means the duplicate is safe to drop.
  for(std::size_t index = 0; index + 1 < messages.size();) {
    if(messages[index].role == Role::User && messages[index + 1].role == Role::User) {
      merge_consecutive_user_message(messages[index], messages[index + 1]);
      messages.erase(messages.begin() + static_cast<std::ptrdiff_t>(index + 1));
    } else {
      ++index;
    }
  }

  for(std::size_t index = 0; index + 1 < messages.size();) {
    if(messages[index].role == messages[index + 1].role && messages[index].content == messages[index + 1].content
        && messages[index].tool_calls == messages[index + 1].tool_calls
        && messages[index].tool_results == messages[index + 1].tool_results
        && messages[index].images == messages[index + 1].images) {
      messages.erase(messages.begin() + static_cast<std::ptrdiff_t>(index + 1));
    } else {
      ++index;
    }
  }
}

void cleanup_interrupted_tools(std::vector<Message>& messages) {
  std::unordered_set<std::string> answered;
  answered.reserve(messages.size());
  for(const auto& message : messages) {
    if(message.role == Role::Tool && message.tool_call_id.has_value()) {
      answered.insert(*message.tool_call_id);
    }
  }

  std::vector<std::string> orphaned;
  for(const auto& message : messages) {
    if(message.role != Role::Assistant) {
      continue;
    }
    for(const auto& call : message.tool_calls) {
      if(!answered.contains(call.id)) {
        orphaned.push_back(call.id);
      }
    }
  }

  for(std::size_t index = 0; index < orphaned.size(); ++index) {
    const auto& call_id = orphaned[index];
    ToolResult result{
        .call_id = call_id,
        .content = "[Tool execution was interrupted]",
        .is_error = true,
    };
    Message tool_message{
        .id = interrupted_tool_message_id(index, call_id),
        .role = Role::Tool,
        .content = result.content,
        .timestamp = std::string{kSyntheticInterruptedToolTimestamp},
        .tool_calls = {},
        .tool_results = {result},
        .tool_call_id = call_id,
    };
    messages.push_back(std::move(tool_message));
  }
}

std::string_view message_tier_kind_to_string(MessageTierKind kind) {
  switch(kind) {
    case MessageTierKind::Steering:
      return "steering";
    case MessageTierKind::FollowUp:
      return "follow_up";
    case MessageTierKind::PostComplete:
      return "post_complete";
  }
  return "steering";
}

std::optional<MessageTierKind> message_tier_kind_from_string(std::string_view value) {
  if(value == "steering") {
    return MessageTierKind::Steering;
  }
  if(value == "follow_up") {
    return MessageTierKind::FollowUp;
  }
  if(value == "post_complete") {
    return MessageTierKind::PostComplete;
  }
  return std::nullopt;
}

}  // namespace ava::types
