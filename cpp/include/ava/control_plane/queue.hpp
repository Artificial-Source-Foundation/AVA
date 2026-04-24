#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace ava::control_plane {

inline constexpr std::string_view kUnsupportedQueueClearError =
    "Clearing follow-up or post-complete queues is not supported yet.";

enum class ClearQueueTarget {
  All,
  Steering,
  FollowUp,
  PostComplete,
};

enum class QueueClearSemantics {
  CancelRunAndClearSteering,
  Unsupported,
};

enum class DeferredQueueSessionErrorKind {
  MissingActiveSession,
  SessionMismatch,
};

struct DeferredQueueSessionError {
  DeferredQueueSessionErrorKind kind{DeferredQueueSessionErrorKind::MissingActiveSession};
  std::optional<std::string> requested_session_id;
  std::optional<std::string> active_session_id;

  [[nodiscard]] std::string message() const;
};

[[nodiscard]] std::string_view clear_queue_target_to_string(ClearQueueTarget target);
[[nodiscard]] std::optional<ClearQueueTarget> parse_clear_queue_target(std::string_view target);
[[nodiscard]] QueueClearSemantics clear_queue_semantics(ClearQueueTarget target);

[[nodiscard]] std::optional<std::string> resolve_deferred_queue_session(
    const std::optional<std::string>& requested_session_id,
    const std::optional<std::string>& active_session_id,
    DeferredQueueSessionError* error = nullptr
);

}  // namespace ava::control_plane
