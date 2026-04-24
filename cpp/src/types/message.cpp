#include "ava/types/message.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <unordered_set>

namespace ava::types {

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
  value.tool_calls.clear();
  if(json.contains("tool_calls")) {
    for(const auto& item : json.at("tool_calls")) {
      value.tool_calls.push_back(ToolCall{
          .id = item.value("id", std::string{}),
          .name = item.value("name", std::string{}),
          .arguments = item.value("arguments", nlohmann::json::object()),
      });
    }
  }

  value.tool_results.clear();
  if(json.contains("tool_results")) {
    for(const auto& item : json.at("tool_results")) {
      value.tool_results.push_back(ToolResult{
          .call_id = item.value("call_id", std::string{}),
          .content = item.value("content", std::string{}),
          .is_error = item.value("is_error", false),
      });
    }
  }
  value.tool_call_id = json.contains("tool_call_id") ? std::optional<std::string>{json.at("tool_call_id").get<std::string>()}
                                                      : std::nullopt;
  value.images.clear();
  if(json.contains("images")) {
    for(const auto& item : json.at("images")) {
      value.images.push_back(ImageContent{
          .data = item.value("data", std::string{}),
          .media_type = item.value("media_type", std::string{}),
      });
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

  for(std::size_t index = 0; index + 1 < messages.size();) {
    if(messages[index].role == Role::User && messages[index + 1].role == Role::User) {
      const auto next_content = messages[index + 1].content;
      if(messages[index].content.empty()) {
        messages[index].content = next_content;
      } else if(!next_content.empty()) {
        messages[index].content += "\n\n" + next_content;
      }
      messages[index].images.insert(
          messages[index].images.end(),
          messages[index + 1].images.begin(),
          messages[index + 1].images.end()
      );
      messages.erase(messages.begin() + static_cast<std::ptrdiff_t>(index + 1));
    } else {
      ++index;
    }
  }

  for(std::size_t index = 0; index + 1 < messages.size();) {
    if(messages[index].role == messages[index + 1].role && messages[index].content == messages[index + 1].content
       && messages[index].tool_calls == messages[index + 1].tool_calls
       && messages[index].tool_results == messages[index + 1].tool_results) {
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

  std::vector<std::pair<std::string, std::string>> orphaned;
  for(const auto& message : messages) {
    if(message.role != Role::Assistant) {
      continue;
    }
    for(const auto& call : message.tool_calls) {
      if(!answered.contains(call.id)) {
        orphaned.emplace_back(call.id, call.name);
      }
    }
  }

  for(const auto& [call_id, _tool_name] : orphaned) {
    ToolResult result{
        .call_id = call_id,
        .content = "[Tool execution was interrupted]",
        .is_error = true,
    };
    Message tool_message{
        .id = "",
        .role = Role::Tool,
        .content = result.content,
        .timestamp = "",
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
