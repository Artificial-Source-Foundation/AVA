#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace ava::types {

enum class Role {
  System,
  User,
  Assistant,
  Tool,
};

[[nodiscard]] std::string_view role_to_string(Role role);
[[nodiscard]] std::optional<Role> role_from_string(std::string_view value);

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
