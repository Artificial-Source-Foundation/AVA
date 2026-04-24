#include "ava/control_plane/sessions.hpp"

#include <cstddef>

namespace ava::control_plane {
namespace {

std::optional<std::size_t> last_user_position(std::span<const ava::types::Message> messages) {
  for(std::size_t index = messages.size(); index > 0; --index) {
    if(messages[index - 1].role == ava::types::Role::User) {
      return index - 1;
    }
  }
  return std::nullopt;
}

SessionReplayPayloadResult build_last_user_replay_payload(
    std::span<const ava::types::Message> messages
) {
  const auto position = last_user_position(messages);
  if(!position.has_value()) {
    return SessionReplayPayloadResult{
        .payload = std::nullopt,
        .error = SessionReplayPayloadError::MissingUserMessage,
    };
  }
  return SessionReplayPayloadResult{
      .payload = load_prompt_context(messages),
      .error = std::nullopt,
  };
}

}  // namespace

std::vector<ava::types::Message> collect_history_before_last_user(std::span<const ava::types::Message> messages) {
  const auto position = last_user_position(messages);
  if(!position.has_value()) {
    return {};
  }
  return std::vector<ava::types::Message>(messages.begin(), messages.begin() + static_cast<std::ptrdiff_t>(*position));
}

SessionPromptContext load_prompt_context(std::span<const ava::types::Message> messages) {
  const auto position = last_user_position(messages);
  if(!position.has_value()) {
    return {};
  }

  const auto& message = messages[*position];
  return SessionPromptContext{
      .goal = message.content,
      .history = collect_history_before_last_user(messages),
      .images = message.images,
  };
}

SessionReplayPayloadResult build_retry_replay_payload(std::span<const ava::types::Message> messages) {
  return build_last_user_replay_payload(messages);
}

SessionReplayPayloadResult build_regenerate_replay_payload(std::span<const ava::types::Message> messages) {
  return build_last_user_replay_payload(messages);
}

SessionReplayPayloadResult build_edit_replay_payload(
    std::span<const ava::types::Message> messages,
    const std::optional<std::string>& message_id,
    std::string new_content
) {
  if(!message_id.has_value()) {
    return SessionReplayPayloadResult{
        .payload = std::nullopt,
        .error = SessionReplayPayloadError::InvalidEditTarget,
    };
  }

  std::optional<std::size_t> position;
  for(std::size_t index = 0; index < messages.size(); ++index) {
    if(messages[index].id == *message_id) {
      position = index;
      break;
    }
  }

  if(!position.has_value()) {
    return SessionReplayPayloadResult{
        .payload = std::nullopt,
        .error = SessionReplayPayloadError::MessageNotFound,
    };
  }

  const auto& target = messages[*position];
  if(target.role != ava::types::Role::User) {
    return SessionReplayPayloadResult{
        .payload = std::nullopt,
        .error = SessionReplayPayloadError::NonUserEditTarget,
    };
  }

  return SessionReplayPayloadResult{
      .payload = SessionPromptContext{
          .goal = std::move(new_content),
          .history = std::vector<ava::types::Message>(
              messages.begin(),
              messages.begin() + static_cast<std::ptrdiff_t>(*position)
          ),
          .images = target.images,
      },
      .error = std::nullopt,
  };
}

std::optional<SessionSelection> resolve_existing_session(
    const std::optional<std::string>& requested_session_id,
    const std::optional<std::string>& last_active_session_id
) {
  if(requested_session_id.has_value()) {
    return SessionSelection{
        .session_id = *requested_session_id,
        .source = SessionSelectionSource::Requested,
    };
  }
  if(last_active_session_id.has_value()) {
    return SessionSelection{
        .session_id = *last_active_session_id,
        .source = SessionSelectionSource::LastActive,
    };
  }
  return std::nullopt;
}

SessionSelection resolve_session_precedence(
    const std::optional<std::string>& requested_session_id,
    const std::optional<std::string>& last_active_session_id,
    std::string new_session_id
) {
  if(auto existing = resolve_existing_session(requested_session_id, last_active_session_id); existing.has_value()) {
    return *existing;
  }
  return SessionSelection{
      .session_id = std::move(new_session_id),
      .source = SessionSelectionSource::New,
  };
}

}  // namespace ava::control_plane
