#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
#include <unordered_map>
#include <vector>

#include "ava/config/agents.hpp"
#include "ava/config/credentials.hpp"
#include "ava/llm/provider.hpp"
#include "ava/orchestration/composition.hpp"
#include "ava/orchestration/subagents.hpp"
#include "ava/tools/registry.hpp"
#include "ava/types/session.hpp"

namespace ava::agent {
struct AgentEvent;
}

namespace ava::orchestration {

struct ChildRunInfo {
  std::string run_id;
  std::string session_id;
  std::string agent_type;
  std::optional<std::string> parent_session_id;
  std::uint32_t depth{0};
};

struct ChildRunTerminalSummary {
  std::string run_id;
  std::string session_id;
  std::string agent_type;
  std::optional<std::string> parent_session_id;
  std::uint32_t depth{0};
  std::string completion_reason;
  bool cancelled{false};
  bool watchdog_timed_out{false};
  std::size_t turns_used{0};
  std::optional<std::string> error;
};

struct TaskResult {
  std::optional<std::string> output;
  std::optional<std::string> error;
  std::string session_id;
  std::vector<ava::types::SessionMessage> messages;
  std::optional<ChildRunTerminalSummary> child_run_summary;

  [[nodiscard]] bool ok() const { return !error.has_value() && !(child_run_summary.has_value() && child_run_summary->cancelled); }
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

struct NativeTaskSpawnerOptions {
  std::filesystem::path session_db_path;
  std::filesystem::path workspace_root{"."};
  std::optional<std::string> provider;
  std::optional<std::string> model;
  std::size_t max_turns{8};
  std::size_t max_spawns{1};
  bool auto_approve{false};
  std::uint32_t parent_depth{0};
  std::optional<std::string> parent_run_id;
  std::optional<std::string> parent_session_id;
  std::optional<std::string> parent_agent_type;
  std::optional<std::string> parent_call_id;
  std::function<bool()> parent_is_cancelled;
  std::shared_ptr<const ava::tools::ToolRegistry> parent_registry;
  ava::config::AgentsConfig agents_config{};
  std::optional<std::chrono::milliseconds> child_run_timeout;
  std::function<void(const ava::agent::AgentEvent&)> event_sink;
  InteractiveApprovalResolver approval_resolver;
  InteractiveQuestionResolver question_resolver;
  InteractivePlanResolver plan_resolver;
  std::function<ava::llm::ProviderPtr(const ResolvedRuntimeSelection&)> provider_factory;
  std::optional<ava::config::CredentialStore> credentials_override;
};

class NativeBlockingTaskSpawner final : public TaskSpawner {
 public:
  explicit NativeBlockingTaskSpawner(NativeTaskSpawnerOptions options);

  TaskResult spawn(const std::string& prompt) override;
  TaskResult spawn_named(const std::string& agent_type, const std::string& prompt) override;

  [[nodiscard]] std::size_t spawn_count_for_testing() const {
    return spawn_count_.load(std::memory_order_acquire);
  }

  [[nodiscard]] std::vector<ChildRunInfo> active_child_runs() const;
  [[nodiscard]] std::optional<ChildRunInfo> active_child_run(const std::string& run_id) const;
  [[nodiscard]] bool cancel_child_run(const std::string& run_id) const;
  [[nodiscard]] std::vector<ChildRunTerminalSummary> child_terminal_summaries() const;
  [[nodiscard]] std::optional<ChildRunTerminalSummary> child_terminal_summary(const std::string& run_id) const;

 private:
  struct ActiveChildRunEntry {
    ChildRunInfo info;
    RunCancellationHandle cancel_handle;
  };

  [[nodiscard]] std::optional<EffectiveSubagentDefinition> definition_for(const std::string& agent_type) const;
  [[nodiscard]] bool is_known_agent_type(const std::string& agent_type) const;
  [[nodiscard]] bool is_disabled_in_config(const std::string& agent_type) const;
  [[nodiscard]] std::optional<std::vector<std::string>> allowed_tools_for(
      const EffectiveSubagentDefinition& definition
  ) const;
  void register_active_child_run(ChildRunInfo info, RunCancellationHandle handle) const;
  void record_child_terminal_summary(ChildRunTerminalSummary summary) const;

  static constexpr std::size_t kMaxChildTerminalSummaries = 256;

  NativeTaskSpawnerOptions options_;
  std::atomic<std::size_t> spawn_count_{0};
  mutable std::mutex child_runs_mutex_;
  mutable std::unordered_map<std::string, ActiveChildRunEntry> active_child_runs_;
  mutable std::vector<ChildRunTerminalSummary> child_terminal_summaries_;
};

// Tiny baseline implementation used by tests and contract consumers.
class NoopTaskSpawner final : public TaskSpawner {
 public:
  TaskResult spawn(const std::string& prompt) override {
    return TaskResult{
        .output = prompt,
        .error = std::nullopt,
        .session_id = "noop-session",
        .messages = {},
    };
  }
};

}  // namespace ava::orchestration
