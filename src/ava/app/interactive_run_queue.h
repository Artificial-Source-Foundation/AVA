#pragma once

#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "ava/app/events.h"
#include "ava/core/result.h"

namespace ava::app {

inline constexpr std::size_t kMaxInteractiveQueuedMessages = 64;
inline constexpr std::size_t kMaxInteractiveQueuedMessageBytes = 64 * 1024;
inline constexpr std::size_t kMaxInteractiveQueueEventMessageBytes = 512;

struct InteractiveQueuedMessage {
  std::string request_id;
  std::string correlation_id;
  std::string message;
  std::size_t sequence = 0;
};

struct InteractiveRestoredMessage {
  std::string request_id;
  std::string message;
  bool steering = false;
};

class InteractiveRunQueue {
 public:
  InteractiveRunQueue(std::string session_id, std::string active_request_id, EventEnvelopeSink event_sink);

  [[nodiscard]] const std::string& active_request_id() const noexcept;
  [[nodiscard]] ava::core::VoidResult queue_steering(std::string message);
  [[nodiscard]] ava::core::VoidResult queue_follow_up(std::string message);
  [[nodiscard]] ava::core::Result<std::vector<std::string>> take_steering_messages();
  [[nodiscard]] ava::core::VoidResult skip_active_steering(std::string_view reason);
  [[nodiscard]] std::optional<InteractiveQueuedMessage> take_next_follow_up();
  [[nodiscard]] ava::core::VoidResult mark_follow_up_started(const InteractiveQueuedMessage& message);
  [[nodiscard]] ava::core::Result<InteractiveRestoredMessage> restore_latest();
  [[nodiscard]] ava::core::VoidResult finish(bool canceled);

 private:
  [[nodiscard]] ava::core::VoidResult queue_message_locked(std::deque<InteractiveQueuedMessage>& queue,
                                                           std::string message, std::string_view event_name);
  [[nodiscard]] ava::core::VoidResult emit_event(std::string_view name, const InteractiveQueuedMessage& message,
                                                 std::string_view reason = {}) const;

  std::string session_id_;
  std::string active_request_id_;
  EventEnvelopeSink event_sink_;
  mutable std::mutex mutex_;
  bool active_ = true;
  std::size_t next_sequence_ = 1;
  std::deque<InteractiveQueuedMessage> steering_messages_;
  std::deque<InteractiveQueuedMessage> follow_up_messages_;
};

}  // namespace ava::app
