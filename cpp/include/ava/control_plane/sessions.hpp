#pragma once

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "ava/types/message.hpp"

namespace ava::control_plane {

enum class SessionSelectionSource {
  Requested,
  LastActive,
  New,
};

struct SessionSelection {
  std::string session_id;
  SessionSelectionSource source{SessionSelectionSource::Requested};
};

struct SessionPromptContext {
  std::string goal;
  std::vector<ava::types::Message> history;
  std::vector<ava::types::ImageContent> images;
};

enum class SessionReplayAction {
  Retry,
  Regenerate,
};

enum class SessionReplayPayloadError {
  MissingUserMessage,
  InvalidEditTarget,
  MessageNotFound,
  NonUserEditTarget,
};

struct SessionReplayPayloadResult {
  std::optional<SessionPromptContext> payload;
  std::optional<SessionReplayPayloadError> error;
};

[[nodiscard]] std::vector<ava::types::Message> collect_history_before_last_user(
    std::span<const ava::types::Message> messages
);

[[nodiscard]] SessionPromptContext load_prompt_context(std::span<const ava::types::Message> messages);

[[nodiscard]] SessionReplayPayloadResult build_retry_replay_payload(std::span<const ava::types::Message> messages);
[[nodiscard]] SessionReplayPayloadResult build_regenerate_replay_payload(std::span<const ava::types::Message> messages);
[[nodiscard]] SessionReplayPayloadResult build_edit_replay_payload(
    std::span<const ava::types::Message> messages,
    const std::optional<std::string>& message_id,
    std::string new_content
);

[[nodiscard]] std::optional<SessionSelection> resolve_existing_session(
    const std::optional<std::string>& requested_session_id,
    const std::optional<std::string>& last_active_session_id
);

[[nodiscard]] SessionSelection resolve_session_precedence(
    const std::optional<std::string>& requested_session_id,
    const std::optional<std::string>& last_active_session_id,
    std::string new_session_id
);

}  // namespace ava::control_plane
