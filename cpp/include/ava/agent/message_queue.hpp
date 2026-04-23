#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <tuple>
#include <vector>

#include "ava/types/message.hpp"

namespace ava::agent {

struct QueuedMessage {
  std::string text;
  ava::types::MessageTier tier{ava::types::MessageTier::steering()};
};

class MessageQueue {
public:
  void enqueue(QueuedMessage message);

  [[nodiscard]] std::vector<std::string> drain_steering();
  [[nodiscard]] std::vector<std::string> drain_follow_up();
  [[nodiscard]] std::tuple<std::uint32_t, std::vector<std::string>> next_post_complete_group();

  [[nodiscard]] bool has_steering() const;
  [[nodiscard]] bool has_follow_up() const;
  [[nodiscard]] bool has_post_complete() const;

  [[nodiscard]] std::tuple<std::size_t, std::size_t, std::size_t> pending_count() const;

  [[nodiscard]] std::uint32_t current_post_group() const;
  void finish_post_complete_group();
  void advance_post_group();
  void clear_steering();

private:
  std::vector<std::string> steering_;
  std::vector<std::string> follow_up_;
  std::map<std::uint32_t, std::vector<std::string>> post_complete_;

  std::uint32_t current_post_group_{1};
  bool group_running_{false};
};

}  // namespace ava::agent
