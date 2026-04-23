#include "ava/types/message.hpp"

#include <array>

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
