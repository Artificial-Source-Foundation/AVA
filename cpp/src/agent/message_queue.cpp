#include "ava/agent/message_queue.hpp"

#include <stdexcept>
#include <utility>

namespace ava::agent {

namespace {

[[nodiscard]] std::size_t post_complete_message_count(const std::map<std::uint32_t, std::vector<std::string>>& groups) {
  std::size_t count = 0;
  for(const auto& [_, messages] : groups) {
    count += messages.size();
  }
  return count;
}

}  // namespace

void MessageQueue::enqueue(QueuedMessage message) {
  switch(message.tier.kind) {
    case ava::types::MessageTierKind::Steering:
      steering_.push_back(std::move(message.text));
      return;
    case ava::types::MessageTierKind::FollowUp:
      follow_up_.push_back(std::move(message.text));
      return;
    case ava::types::MessageTierKind::PostComplete: {
      const auto group = message.tier.post_complete_group == 0 ? current_post_group() : message.tier.post_complete_group;
      post_complete_[group].push_back(std::move(message.text));
      return;
    }
  }
}

std::vector<std::string> MessageQueue::drain_steering() {
  auto drained = std::move(steering_);
  steering_.clear();
  return drained;
}

std::vector<std::string> MessageQueue::drain_follow_up() {
  auto drained = std::move(follow_up_);
  follow_up_.clear();
  return drained;
}

std::tuple<std::uint32_t, std::vector<std::string>> MessageQueue::next_post_complete_group() {
  if(post_complete_.empty()) {
    throw std::runtime_error("no post-complete group available");
  }

  auto it = post_complete_.begin();
  auto group = it->first;
  auto messages = std::move(it->second);
  post_complete_.erase(it);
  group_running_ = true;
  return std::make_tuple(group, std::move(messages));
}

bool MessageQueue::has_steering() const {
  return !steering_.empty();
}

bool MessageQueue::has_follow_up() const {
  return !follow_up_.empty();
}

bool MessageQueue::has_post_complete() const {
  return !post_complete_.empty();
}

std::tuple<std::size_t, std::size_t, std::size_t> MessageQueue::pending_count() const {
  return std::make_tuple(steering_.size(), follow_up_.size(), post_complete_message_count(post_complete_));
}

std::uint32_t MessageQueue::current_post_group() const {
  if(group_running_) {
    return current_post_group_ + 1;
  }
  return current_post_group_;
}

void MessageQueue::finish_post_complete_group() {
  group_running_ = false;
}

void MessageQueue::advance_post_group() {
  ++current_post_group_;
}

void MessageQueue::clear_steering() {
  steering_.clear();
}

}  // namespace ava::agent
