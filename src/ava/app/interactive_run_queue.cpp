#include "ava/app/interactive_run_queue.h"

#include <string_view>
#include <utility>

#include "ava/core/error.h"
#include "ava/core/ids.h"
#include "ava/session/session_store.h"

namespace ava::app {
namespace {

std::string_view utf8_prefix_within(std::string_view value, std::size_t max_bytes)
{
  if (value.size() <= max_bytes) return value;

  std::size_t index = 0;
  std::size_t end = 0;
  while (index < value.size() && index < max_bytes) {
    auto const byte = static_cast<unsigned char>(value[index]);
    std::size_t length = 0;
    if (byte < 0x80U) {
      length = 1;
    } else if ((byte & 0xE0U) == 0xC0U) {
      length = 2;
    } else if ((byte & 0xF0U) == 0xE0U) {
      length = 3;
    } else if ((byte & 0xF8U) == 0xF0U) {
      length = 4;
    } else {
      break;
    }
    if (index + length > max_bytes || index + length > value.size()) break;
    bool valid_continuation = true;
    for (std::size_t offset = 1; offset < length; ++offset) {
      auto const continuation = static_cast<unsigned char>(value[index + offset]);
      if ((continuation & 0xC0U) != 0x80U) {
        valid_continuation = false;
        break;
      }
    }
    if (!valid_continuation) break;
    end = index + length;
    index += length;
  }
  return value.substr(0, end);
}

std::size_t queued_message_bytes(std::deque<InteractiveQueuedMessage> const& messages)
{
  std::size_t bytes = 0;
  for (auto const& message : messages) bytes += message.message.size();
  return bytes;
}

std::string json_string_field(std::string_view key, std::string_view value)
{
  return "\"" + std::string(key) + "\":\"" + ava::session::json_escape(value) + "\"";
}

std::string queue_payload_json(std::string_view message, std::string_view reason)
{
  bool const truncated = message.size() > kMaxInteractiveQueueEventMessageBytes;
  auto const event_message = truncated ? utf8_prefix_within(message, kMaxInteractiveQueueEventMessageBytes) : message;

  std::string json = "{";
  json += json_string_field("message", event_message);
  if (truncated) {
    json += ",\"message_truncated\":true,\"message_bytes\":" + std::to_string(message.size());
  }
  if (!reason.empty()) {
    json += ',';
    json += json_string_field("reason", reason);
  }
  json += '}';
  return json;
}

ava::core::Error inactive_error()
{
  return ava::core::Error(ava::core::ErrorCategory::InvalidArgument,
                          "interactive queue requires an active backend run");
}

ava::core::Error empty_message_error()
{
  return ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "interactive queued message is empty");
}

ava::core::Error queue_limit_error()
{
  auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "interactive queued message limit exceeded");
  error.with_context("max_entries", std::to_string(kMaxInteractiveQueuedMessages));
  error.with_context("max_message_bytes", std::to_string(kMaxInteractiveQueuedMessageBytes));
  return error;
}

ava::core::Error no_queued_messages_error()
{
  return ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "no active queued message to restore");
}

}  // namespace

InteractiveRunQueue::InteractiveRunQueue(std::string session_id, std::string active_request_id,
                                         EventEnvelopeSink event_sink)
    : session_id_(std::move(session_id)),
      active_request_id_(std::move(active_request_id)),
      event_sink_(std::move(event_sink))
{
}

std::string const& InteractiveRunQueue::active_request_id() const noexcept
{
  return active_request_id_;
}

ava::core::VoidResult InteractiveRunQueue::queue_steering(std::string message)
{
  std::lock_guard lock(mutex_);
  return queue_message_locked(steering_messages_, std::move(message), "steer_queued");
}

ava::core::VoidResult InteractiveRunQueue::queue_follow_up(std::string message)
{
  std::lock_guard lock(mutex_);
  return queue_message_locked(follow_up_messages_, std::move(message), "follow_up_queued");
}

ava::core::Result<std::vector<std::string>> InteractiveRunQueue::take_steering_messages()
{
  std::lock_guard lock(mutex_);
  std::vector<std::string> messages;
  std::deque<InteractiveQueuedMessage> remaining;
  while (!steering_messages_.empty()) {
    auto item = std::move(steering_messages_.front());
    steering_messages_.pop_front();
    if (item.correlation_id == active_request_id_) {
      if (auto emitted = emit_event("steer_applied", item); !emitted) {
        return std::unexpected(std::move(emitted.error()));
      }
      messages.push_back(std::move(item.message));
    } else {
      remaining.push_back(std::move(item));
    }
  }
  steering_messages_ = std::move(remaining);
  return messages;
}

ava::core::VoidResult InteractiveRunQueue::skip_active_steering(std::string_view reason)
{
  std::lock_guard lock(mutex_);
  std::deque<InteractiveQueuedMessage> remaining;
  while (!steering_messages_.empty()) {
    auto item = std::move(steering_messages_.front());
    steering_messages_.pop_front();
    if (item.correlation_id == active_request_id_) {
      if (auto emitted = emit_event("steer_skipped", item, reason); !emitted) return emitted;
    } else {
      remaining.push_back(std::move(item));
    }
  }
  steering_messages_ = std::move(remaining);
  return {};
}

std::optional<InteractiveQueuedMessage> InteractiveRunQueue::take_next_follow_up()
{
  std::lock_guard lock(mutex_);
  if (!active_ || follow_up_messages_.empty()) return std::nullopt;
  auto next = std::move(follow_up_messages_.front());
  follow_up_messages_.pop_front();
  return next;
}

ava::core::VoidResult InteractiveRunQueue::mark_follow_up_started(InteractiveQueuedMessage const& message)
{
  std::lock_guard lock(mutex_);
  if (!active_) return std::unexpected(inactive_error());
  active_request_id_ = message.request_id;
  auto started = message;
  started.correlation_id = message.request_id;
  return emit_event("follow_up_started", started);
}

ava::core::Result<InteractiveRestoredMessage> InteractiveRunQueue::restore_latest()
{
  std::lock_guard lock(mutex_);
  if (!active_) return std::unexpected(inactive_error());

  auto const restore_from_steering =
      !steering_messages_.empty() &&
      (follow_up_messages_.empty() || steering_messages_.back().sequence > follow_up_messages_.back().sequence);
  auto& queue = restore_from_steering ? steering_messages_ : follow_up_messages_;
  if (queue.empty()) return std::unexpected(no_queued_messages_error());

  auto const& restored = queue.back();
  auto const event_name =
      restore_from_steering ? std::string_view("steer_skipped") : std::string_view("follow_up_skipped");
  if (auto emitted = emit_event(event_name, restored, "restored_to_composer"); !emitted) {
    return std::unexpected(std::move(emitted.error()));
  }
  auto result = InteractiveRestoredMessage{
      .request_id = restored.request_id, .message = restored.message, .steering = restore_from_steering};
  queue.pop_back();
  return result;
}

ava::core::VoidResult InteractiveRunQueue::finish(bool canceled)
{
  std::lock_guard lock(mutex_);
  active_ = false;

  auto const reason = canceled ? std::string_view("canceled") : std::string_view("run_completed_before_safe_point");
  while (!steering_messages_.empty()) {
    auto const& item = steering_messages_.front();
    if (auto emitted = emit_event("steer_skipped", item, reason); !emitted) return emitted;
    steering_messages_.pop_front();
  }
  while (!follow_up_messages_.empty()) {
    auto const& item = follow_up_messages_.front();
    if (auto emitted = emit_event("follow_up_skipped", item, reason); !emitted) return emitted;
    follow_up_messages_.pop_front();
  }
  return {};
}

ava::core::VoidResult InteractiveRunQueue::queue_message_locked(std::deque<InteractiveQueuedMessage>& queue,
                                                                std::string message, std::string_view event_name)
{
  if (message.empty()) return std::unexpected(empty_message_error());
  if (!active_) return std::unexpected(inactive_error());
  if (queue.size() >= kMaxInteractiveQueuedMessages || message.size() > kMaxInteractiveQueuedMessageBytes ||
      queued_message_bytes(queue) + message.size() > kMaxInteractiveQueuedMessageBytes) {
    return std::unexpected(queue_limit_error());
  }

  InteractiveQueuedMessage queued{.request_id = ava::core::make_id("request"),
                                  .correlation_id = active_request_id_,
                                  .message = std::move(message),
                                  .sequence = next_sequence_++};
  queue.push_back(queued);
  if (auto emitted = emit_event(event_name, queued); !emitted) {
    queue.pop_back();
    return emitted;
  }
  return {};
}

ava::core::VoidResult InteractiveRunQueue::emit_event(std::string_view name, InteractiveQueuedMessage const& message,
                                                      std::string_view reason) const
{
  if (!event_sink_) return {};

  EventEnvelope envelope;
  envelope.schema_version = 1;
  envelope.event_id = ava::core::make_id("event");
  envelope.timestamp = ava::session::now_timestamp();
  envelope.session_id = session_id_;
  envelope.request_id = message.request_id;
  envelope.correlation_id = message.correlation_id;
  envelope.name = std::string(name);
  envelope.payload_json = queue_payload_json(message.message, reason);
  return event_sink_(envelope);
}

}  // namespace ava::app
