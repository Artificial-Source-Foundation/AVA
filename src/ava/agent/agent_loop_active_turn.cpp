#include "ava/agent/agent_loop_active_turn.h"

#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "ava/agent/session_recorder.h"

namespace ava::agent {
namespace {

template <typename Callback>
auto with_session_lock(std::mutex* session_mutex, Callback&& callback) -> decltype(callback())
{
  if (session_mutex) {
    std::lock_guard lock(*session_mutex);
    return callback();
  }
  return callback();
}

}  // namespace

std::vector<ActiveTurnMessage> const& ActiveTurnMessages::messages() const noexcept
{
  return messages_;
}

std::vector<std::string> ActiveTurnMessages::replayable_texts() const
{
  std::vector<std::string> texts;
  texts.reserve(messages_.size());
  for (auto const& message : messages_) texts.push_back(message.text);
  return texts;
}

ava::core::VoidResult ActiveTurnMessages::append_user_message(ava::session::SessionStore& store,
                                                              std::mutex* session_mutex, std::string const& text)
{
  auto appended = with_session_lock(session_mutex, [&]() { return ava::agent::append_user_message(store, text); });
  if (!appended) return std::unexpected(std::move(appended.error()));
  messages_.push_back(ActiveTurnMessage{.id = *appended, .text = text});
  return {};
}

ava::core::VoidResult ActiveTurnMessages::replay_user_messages(ava::session::SessionStore& store,
                                                               std::mutex* session_mutex) const
{
  for (auto const& message : messages_) {
    auto replayed = with_session_lock(
        session_mutex, [&]() { return ava::agent::append_replay_user_message(store, message.text, message.id); });
    if (!replayed) return replayed;
  }
  return {};
}

}  // namespace ava::agent
