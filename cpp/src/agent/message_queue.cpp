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

MessageQueue::MessageQueue(MessageQueue&& other) noexcept {
  const std::lock_guard<std::mutex> lock(*other.mutex_);
  steering_ = std::move(other.steering_);
  follow_up_ = std::move(other.follow_up_);
  post_complete_ = std::move(other.post_complete_);
  current_post_group_ = other.current_post_group_;
  group_running_ = other.group_running_;
  mutex_ = std::make_unique<std::mutex>();
}

MessageQueue& MessageQueue::operator=(MessageQueue&& other) noexcept {
  if(this == &other) {
    return *this;
  }

  const std::scoped_lock lock(*mutex_, *other.mutex_);
  steering_ = std::move(other.steering_);
  follow_up_ = std::move(other.follow_up_);
  post_complete_ = std::move(other.post_complete_);
  current_post_group_ = other.current_post_group_;
  group_running_ = other.group_running_;
  return *this;
}

void MessageQueue::enqueue(QueuedMessage message) {
  const std::lock_guard<std::mutex> lock(*mutex_);
  switch(message.tier.kind) {
    case ava::types::MessageTierKind::Steering:
      steering_.push_back(std::move(message.text));
      return;
    case ava::types::MessageTierKind::FollowUp:
      follow_up_.push_back(std::move(message.text));
      return;
    case ava::types::MessageTierKind::PostComplete: {
      const auto group = message.tier.post_complete_group == 0
                             ? (group_running_ ? current_post_group_ + 1 : current_post_group_)
                             : message.tier.post_complete_group;
      post_complete_[group].push_back(std::move(message.text));
      return;
    }
  }
}

std::vector<std::string> MessageQueue::drain_steering() {
  const std::lock_guard<std::mutex> lock(*mutex_);
  auto drained = std::move(steering_);
  steering_.clear();
  return drained;
}

std::vector<std::string> MessageQueue::drain_follow_up() {
  const std::lock_guard<std::mutex> lock(*mutex_);
  auto drained = std::move(follow_up_);
  follow_up_.clear();
  return drained;
}

std::tuple<std::uint32_t, std::vector<std::string>> MessageQueue::next_post_complete_group() {
  const std::lock_guard<std::mutex> lock(*mutex_);
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
  const std::lock_guard<std::mutex> lock(*mutex_);
  return !steering_.empty();
}

bool MessageQueue::has_follow_up() const {
  const std::lock_guard<std::mutex> lock(*mutex_);
  return !follow_up_.empty();
}

bool MessageQueue::has_post_complete() const {
  const std::lock_guard<std::mutex> lock(*mutex_);
  return !post_complete_.empty();
}

std::tuple<std::size_t, std::size_t, std::size_t> MessageQueue::pending_count() const {
  const std::lock_guard<std::mutex> lock(*mutex_);
  return std::make_tuple(steering_.size(), follow_up_.size(), post_complete_message_count(post_complete_));
}

std::uint32_t MessageQueue::current_post_group() const {
  const std::lock_guard<std::mutex> lock(*mutex_);
  if(group_running_) {
    return current_post_group_ + 1;
  }
  return current_post_group_;
}

void MessageQueue::finish_post_complete_group() {
  const std::lock_guard<std::mutex> lock(*mutex_);
  group_running_ = false;
}

void MessageQueue::advance_post_group() {
  const std::lock_guard<std::mutex> lock(*mutex_);
  ++current_post_group_;
}

void MessageQueue::clear_steering() {
  const std::lock_guard<std::mutex> lock(*mutex_);
  steering_.clear();
}

}  // namespace ava::agent
