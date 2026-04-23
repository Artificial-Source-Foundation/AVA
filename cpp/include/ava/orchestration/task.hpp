#pragma once

#include <string>
#include <utility>
#include <vector>

#include "ava/types/session.hpp"

namespace ava::orchestration {

struct TaskResult {
  std::string text;
  std::string session_id;
  std::vector<ava::types::SessionMessage> messages;
};

class TaskSpawner {
 public:
  virtual ~TaskSpawner() = default;

  virtual TaskResult spawn(const std::string& prompt) = 0;

  virtual TaskResult spawn_named(const std::string& agent_type, const std::string& prompt) {
    (void)agent_type;
    return spawn(prompt);
  }
};

// Tiny baseline implementation used by tests and contract consumers.
class NoopTaskSpawner final : public TaskSpawner {
 public:
  TaskResult spawn(const std::string& prompt) override {
    return TaskResult{
        .text = prompt,
        .session_id = "noop-session",
        .messages = {},
    };
  }
};

}  // namespace ava::orchestration
