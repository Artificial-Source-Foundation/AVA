#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <nlohmann/json.hpp>

#include "ava/types/tool.hpp"

namespace ava::types {

enum class Role {
  System,
  User,
  Assistant,
  Tool,
};

[[nodiscard]] std::string_view role_to_string(Role role);
[[nodiscard]] std::optional<Role> role_from_string(std::string_view value);

struct ImageContent {
  std::string data;
  std::string media_type;
};

struct Message {
  std::string id;
  Role role{Role::User};
  std::string content;
  std::string timestamp;
  std::vector<ToolCall> tool_calls;
  std::vector<ToolResult> tool_results;
  std::optional<std::string> tool_call_id;
  std::vector<ImageContent> images;
  std::optional<std::string> parent_id;
  bool agent_visible{true};
  bool user_visible{true};
  std::optional<std::string> original_content;
  nlohmann::json structured_content = nlohmann::json::array();
  nlohmann::json metadata = nlohmann::json::object();
};

void to_json(nlohmann::json& json, const ImageContent& value);
void from_json(const nlohmann::json& json, ImageContent& value);

void to_json(nlohmann::json& json, const Message& value);
void from_json(const nlohmann::json& json, Message& value);

void repair_conversation(std::vector<Message>& messages);
void cleanup_interrupted_tools(std::vector<Message>& messages);

enum class MessageTierKind {
  Steering,
  FollowUp,
  PostComplete,
};

struct MessageTier {
  MessageTierKind kind{MessageTierKind::Steering};
  std::uint32_t post_complete_group{1};

  [[nodiscard]] static MessageTier steering() { return MessageTier{MessageTierKind::Steering, 1}; }
  [[nodiscard]] static MessageTier follow_up() { return MessageTier{MessageTierKind::FollowUp, 1}; }
  [[nodiscard]] static MessageTier post_complete(std::uint32_t group = 1) {
    return MessageTier{MessageTierKind::PostComplete, group == 0 ? 1U : group};
  }
};

[[nodiscard]] std::string_view message_tier_kind_to_string(MessageTierKind kind);
[[nodiscard]] std::optional<MessageTierKind> message_tier_kind_from_string(std::string_view value);

}  // namespace ava::types
