#include "ava/control_plane/queue.hpp"

namespace ava::control_plane {

std::string DeferredQueueSessionError::message() const {
  switch(kind) {
    case DeferredQueueSessionErrorKind::MissingActiveSession:
      return "Agent queue has no active session owner.";
    case DeferredQueueSessionErrorKind::SessionMismatch:
      return "Requested session " + requested_session_id.value_or("")
             + " does not match active queued-run session " + active_session_id.value_or("") + ".";
  }
  return "Agent queue has no active session owner.";
}

std::string_view clear_queue_target_to_string(ClearQueueTarget target) {
  switch(target) {
    case ClearQueueTarget::All:
      return "all";
    case ClearQueueTarget::Steering:
      return "steering";
    case ClearQueueTarget::FollowUp:
      return "followUp";
    case ClearQueueTarget::PostComplete:
      return "postComplete";
  }
  return "all";
}

std::optional<ClearQueueTarget> parse_clear_queue_target(std::string_view target) {
  if(target == "all" || target == "All") {
    return ClearQueueTarget::All;
  }
  if(target == "steering" || target == "Steering") {
    return ClearQueueTarget::Steering;
  }
  if(target == "followUp" || target == "follow_up" || target == "follow-up" || target == "followup"
     || target == "FollowUp") {
    return ClearQueueTarget::FollowUp;
  }
  if(target == "postComplete" || target == "post_complete" || target == "post-complete" || target == "postcomplete"
     || target == "PostComplete") {
    return ClearQueueTarget::PostComplete;
  }
  return std::nullopt;
}

QueueClearSemantics clear_queue_semantics(ClearQueueTarget target) {
  switch(target) {
    case ClearQueueTarget::All:
    case ClearQueueTarget::Steering:
      return QueueClearSemantics::CancelRunAndClearSteering;
    case ClearQueueTarget::FollowUp:
    case ClearQueueTarget::PostComplete:
      return QueueClearSemantics::Unsupported;
  }
  return QueueClearSemantics::CancelRunAndClearSteering;
}

std::optional<std::string> resolve_deferred_queue_session(
    const std::optional<std::string>& requested_session_id,
    const std::optional<std::string>& active_session_id,
    DeferredQueueSessionError* error
) {
  if(!active_session_id.has_value()) {
    if(error != nullptr) {
      *error = DeferredQueueSessionError{
          .kind = DeferredQueueSessionErrorKind::MissingActiveSession,
          .requested_session_id = std::nullopt,
          .active_session_id = std::nullopt,
      };
    }
    return std::nullopt;
  }

  if(requested_session_id.has_value() && *requested_session_id != *active_session_id) {
    if(error != nullptr) {
      *error = DeferredQueueSessionError{
          .kind = DeferredQueueSessionErrorKind::SessionMismatch,
          .requested_session_id = requested_session_id,
          .active_session_id = active_session_id,
      };
    }
    return std::nullopt;
  }

  return active_session_id;
}

}  // namespace ava::control_plane
