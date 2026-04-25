#include "ava/orchestration/task.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <stdexcept>
#include <string>
#include <utility>

#include "ava/agent/runtime.hpp"
#include "ava/orchestration/subagents.hpp"

namespace ava::orchestration {

namespace {

class SpawnBudgetGuard {
 public:
  explicit SpawnBudgetGuard(std::atomic<std::size_t>& counter) : counter_(counter) {}
  ~SpawnBudgetGuard() {
    if(active_) {
      counter_.fetch_sub(1, std::memory_order_relaxed);
    }
  }

  void release() { active_ = false; }

 private:
  std::atomic<std::size_t>& counter_;
  bool active_{true};
};

[[nodiscard]] bool has_text(const std::optional<std::string>& value) {
  return value.has_value() && value->find_first_not_of(" \t\r\n") != std::string::npos;
}

[[nodiscard]] std::string runtime_profile_to_string(SubAgentRuntimeProfile profile) {
  switch(profile) {
    case SubAgentRuntimeProfile::Full:
      return "full";
    case SubAgentRuntimeProfile::ReadOnly:
      return "read_only";
  }
  return "full";
}

[[nodiscard]] std::string completion_reason_to_string(ava::agent::AgentCompletionReason reason) {
  switch(reason) {
    case ava::agent::AgentCompletionReason::Completed:
      return "completed";
    case ava::agent::AgentCompletionReason::Cancelled:
      return "cancelled";
    case ava::agent::AgentCompletionReason::MaxTurns:
      return "max_turns";
    case ava::agent::AgentCompletionReason::Stuck:
      return "stuck";
    case ava::agent::AgentCompletionReason::Error:
      return "error";
  }
  return "error";
}

}  // namespace

NativeBlockingTaskSpawner::NativeBlockingTaskSpawner(NativeTaskSpawnerOptions options)
    : options_(std::move(options)) {
  if(options_.session_db_path.empty()) {
    throw std::invalid_argument("NativeBlockingTaskSpawner requires a session_db_path");
  }
  if(options_.max_turns == 0) {
    throw std::invalid_argument("NativeBlockingTaskSpawner requires max_turns >= 1");
  }
}

TaskResult NativeBlockingTaskSpawner::spawn(const std::string& prompt) {
  return spawn_named("subagent", prompt);
}

TaskResult NativeBlockingTaskSpawner::spawn_named(const std::string& agent_type, const std::string& prompt) {
  if(agent_type.empty()) {
    throw std::invalid_argument("agent_type must not be empty");
  }

  if(prompt.empty()) {
    throw std::invalid_argument("subagent prompt must not be empty");
  }

  const auto next_depth = options_.parent_depth + 1;
  if(next_depth > MAX_AGENT_DEPTH) {
    throw std::runtime_error(
        "subagent depth limit exceeded: max=" + std::to_string(MAX_AGENT_DEPTH) +
        " requested=" + std::to_string(next_depth)
    );
  }

  if(is_disabled_in_config(agent_type)) {
    throw std::runtime_error("subagent '" + agent_type + "' is disabled in configuration");
  }

  const auto maybe_definition = definition_for(agent_type);
  if(!maybe_definition.has_value() && is_known_agent_type(agent_type)) {
    throw std::runtime_error("subagent '" + agent_type + "' is disabled by default configuration");
  }

  if(options_.max_spawns == 0) {
    throw std::runtime_error("subagent delegation is disabled for this task");
  }

  const auto next_spawn = spawn_count_.fetch_add(1, std::memory_order_relaxed) + 1;
  SpawnBudgetGuard budget_guard(spawn_count_);
  if(next_spawn > options_.max_spawns) {
    throw std::runtime_error("subagent budget exhausted (max " + std::to_string(options_.max_spawns) + ")");
  }

  EffectiveSubagentDefinition definition;
  if(maybe_definition.has_value()) {
    definition = *maybe_definition;
  } else {
    definition.id = agent_type;
    definition.runtime_profile = runtime_profile_for(agent_type);
  }

  RuntimeSelectionOptions selection{};
  const auto requested_turns = definition.max_turns.value_or(options_.max_turns);
  selection.max_turns = std::max<std::size_t>(1, std::min(requested_turns, options_.max_turns));
  selection.max_turns_explicit = true;

  std::optional<std::string> provider = options_.provider;
  std::optional<std::string> model = options_.model;

  if(definition.model.has_value()) {
    const auto& model_spec = *definition.model;
    const auto has_explicit_model_provider = model_spec.find('/') != std::string::npos;
    if(has_explicit_model_provider) {
      const auto parsed = parse_model_spec(model_spec);
      model = parsed.model;
      if(definition.provider.has_value()) {
        const auto explicit_provider = ava::config::normalize_provider_name(*definition.provider);
        const auto model_provider = ava::config::normalize_provider_name(parsed.provider);
        if(explicit_provider != model_provider) {
          throw std::runtime_error(
              "conflicting subagent provider/model configuration: provider='" + explicit_provider +
              "' model='" + model_spec + "'"
          );
        }
        provider = explicit_provider;
      } else {
        provider = parsed.provider;
      }
    } else {
      model = model_spec;
      const auto parsed_alias = parse_model_spec(model_spec);
      if(definition.provider.has_value()) {
        const auto explicit_provider = ava::config::normalize_provider_name(*definition.provider);
        const auto alias_provider = ava::config::normalize_provider_name(parsed_alias.provider);
        if(parsed_alias.model != model_spec && explicit_provider != alias_provider) {
          throw std::runtime_error(
              "conflicting subagent provider/model configuration: provider='" + explicit_provider +
              "' model alias='" + model_spec + "' resolves to provider='" + alias_provider + "'"
          );
        }
        provider = explicit_provider;
        if(parsed_alias.model != model_spec) {
          model = parsed_alias.model;
        }
      } else if(!provider.has_value()) {
        provider = parsed_alias.provider;
        model = parsed_alias.model;
      } else if(parsed_alias.model != model_spec) {
        const auto inherited_provider = ava::config::normalize_provider_name(*provider);
        const auto alias_provider = ava::config::normalize_provider_name(parsed_alias.provider);
        if(inherited_provider != alias_provider) {
          throw std::runtime_error(
              "conflicting subagent provider/model configuration: inherited provider='" + inherited_provider +
              "' model alias='" + model_spec + "' resolves to provider='" + alias_provider + "'"
          );
        }
        model = parsed_alias.model;
      }
    }
  } else if(definition.provider.has_value()) {
    provider = *definition.provider;
  }

  selection.provider = provider;
  selection.model = model;

  RuntimeCompositionRequest request{
      .session_db_path = options_.session_db_path,
      .workspace_root = options_.workspace_root,
      .resume_latest = false,
      .session_id = std::nullopt,
      .selection = selection,
      .auto_approve = options_.auto_approve,
      .allowed_tools = allowed_tools_for(definition),
      .system_prompt_preamble = build_subagent_system_prompt(agent_type),
      .approval_resolver = options_.approval_resolver,
      .question_resolver = options_.question_resolver,
      .plan_resolver = options_.plan_resolver,
      .provider_override = nullptr,
      .provider_factory = options_.provider_factory,
      .credentials_override = options_.credentials_override,
      .load_global_mcp_config = true,
  };

  auto composition = compose_runtime(std::move(request));

  composition.session.metadata["orchestration"]["subagent_run"] = {
      {"agent_type", agent_type},
      {"runtime_profile", runtime_profile_to_string(definition.runtime_profile)},
      {"depth", next_depth},
      {"parent_depth", options_.parent_depth},
      {"parent_session_id", options_.parent_session_id.value_or("")},
      {"parent_agent_type", options_.parent_agent_type.value_or("")},
  };

  const auto deadline = options_.child_run_timeout.has_value()
                            ? std::optional<std::chrono::steady_clock::time_point>{
                                  std::chrono::steady_clock::now() + *options_.child_run_timeout
                              }
                            : std::nullopt;
  const auto run_lease = composition.run_controller->begin_run(deadline);
  composition.interactive_bridge->set_run_id(run_lease.run_id);
  const auto persist_child_metadata = [&](const std::string& completion_reason,
                                          bool watchdog_timed_out,
                                          std::size_t turns_used,
                                          const std::optional<std::string>& error) {
    auto& metadata = composition.session.metadata["orchestration"]["subagent_run"];
    metadata["turns_used"] = turns_used;
    metadata["run_id"] = run_lease.run_id;
    metadata["completion_reason"] = completion_reason;
    metadata["watchdog_timed_out"] = watchdog_timed_out;
    if(error.has_value()) {
      metadata["error"] = *error;
    } else {
      metadata.erase("error");
    }
  };
  register_active_child_run(
      ChildRunInfo{
          .run_id = run_lease.run_id,
          .session_id = composition.session.id,
          .agent_type = agent_type,
          .parent_session_id = options_.parent_session_id,
          .depth = next_depth,
      },
      run_lease.handle
  );
  ava::agent::AgentRunResult run_result;
  std::atomic<bool> deadline_cancelled{false};
  try {
    const auto token = run_lease.token;
    const auto parent_is_cancelled = options_.parent_is_cancelled;
    run_result = composition.runtime->run(
        composition.session,
        ava::agent::AgentRunInput{
            .goal = prompt,
            .queue = &composition.queue,
            .run_id = run_lease.run_id,
            .is_cancelled = [token, parent_is_cancelled, &deadline_cancelled] {
              if(token.is_deadline_expired()) {
                deadline_cancelled.store(true, std::memory_order_release);
                return true;
              }
              return token.is_cancelled() || (parent_is_cancelled && parent_is_cancelled());
            },
            .stream = true,
        }
    );
  } catch(const std::exception& error) {
    composition.interactive_bridge->set_run_id(std::nullopt);
    try {
      persist_child_metadata("error", false, 0, error.what());
      composition.save_session();
    } catch(...) {
    }
    try {
      record_child_terminal_summary(ChildRunTerminalSummary{
          .run_id = run_lease.run_id,
          .session_id = composition.session.id,
          .agent_type = agent_type,
          .parent_session_id = options_.parent_session_id,
          .depth = next_depth,
          .completion_reason = "error",
          .cancelled = false,
          .watchdog_timed_out = false,
          .turns_used = 0,
          .error = error.what(),
      });
    } catch(...) {
    }
    throw;
  } catch(...) {
    composition.interactive_bridge->set_run_id(std::nullopt);
    try {
      persist_child_metadata("error", false, 0, "child run failed with non-standard exception");
      composition.save_session();
    } catch(...) {
    }
    try {
      record_child_terminal_summary(ChildRunTerminalSummary{
          .run_id = run_lease.run_id,
          .session_id = composition.session.id,
          .agent_type = agent_type,
          .parent_session_id = options_.parent_session_id,
          .depth = next_depth,
          .completion_reason = "error",
          .cancelled = false,
          .watchdog_timed_out = false,
          .turns_used = 0,
          .error = "child run failed with non-standard exception",
      });
    } catch(...) {
    }
    throw;
  }
  composition.interactive_bridge->set_run_id(std::nullopt);

  const auto watchdog_timed_out = run_result.reason == ava::agent::AgentCompletionReason::Cancelled
                                    && deadline_cancelled.load(std::memory_order_acquire);
  const auto child_completion_reason = watchdog_timed_out ? std::string{"watchdog_timeout"}
                                                          : completion_reason_to_string(run_result.reason);

  persist_child_metadata(child_completion_reason, watchdog_timed_out, run_result.turns_used, run_result.error);

  const auto terminal_error = run_result.error.has_value()
                                  ? run_result.error
                                  : (run_result.reason == ava::agent::AgentCompletionReason::Cancelled
                                         ? std::optional<std::string>{watchdog_timed_out ? "child run watchdog timeout"
                                                                                        : "child run cancelled"}
                                         : std::nullopt);

  const auto child_summary = ChildRunTerminalSummary{
      .run_id = run_lease.run_id,
      .session_id = composition.session.id,
      .agent_type = agent_type,
      .parent_session_id = options_.parent_session_id,
      .depth = next_depth,
      .completion_reason = child_completion_reason,
      .cancelled = run_result.reason == ava::agent::AgentCompletionReason::Cancelled,
      .watchdog_timed_out = watchdog_timed_out,
      .turns_used = run_result.turns_used,
      .error = terminal_error,
  };
  record_child_terminal_summary(child_summary);

  composition.save_session();
  const auto should_emit_subagent_complete = run_result.reason == ava::agent::AgentCompletionReason::Completed ||
                                             run_result.reason == ava::agent::AgentCompletionReason::MaxTurns;
  if(options_.event_sink && has_text(options_.parent_run_id) && has_text(options_.parent_call_id) &&
     should_emit_subagent_complete && !terminal_error.has_value()) {
    try {
      options_.event_sink(ava::agent::AgentEvent{
          .kind = ava::agent::AgentEventKind::SubagentComplete,
          .run_id = options_.parent_run_id,
          .subagent_call_id = options_.parent_call_id,
          .subagent_session_id = composition.session.id,
          .subagent_description = prompt,
          .subagent_message_count = composition.session.messages.size(),
      });
    } catch(...) {
      // Event projection is observational; child results and persistence must still complete.
    }
  }
  // Persisted successfully: consume one spawn budget unit.
  budget_guard.release();

  TaskResult result{
      .output = terminal_error.has_value() ? std::nullopt : std::optional<std::string>{run_result.final_response},
      .error = terminal_error,
      .session_id = composition.session.id,
      .messages = std::move(composition.session.messages),
      .child_run_summary = child_summary,
  };
  return result;
}

std::optional<EffectiveSubagentDefinition> NativeBlockingTaskSpawner::definition_for(const std::string& agent_type) const {
  const auto definitions = effective_subagent_definitions(options_.agents_config);
  const auto it = std::find_if(definitions.begin(), definitions.end(), [&](const auto& definition) {
    return definition.id == agent_type;
  });
  if(it == definitions.end()) {
    return std::nullopt;
  }
  return *it;
}

bool NativeBlockingTaskSpawner::is_known_agent_type(const std::string& agent_type) const {
  const auto builtin_ids = builtin_subagent_ids();
  if(std::find(builtin_ids.begin(), builtin_ids.end(), agent_type) != builtin_ids.end()) {
    return true;
  }
  return std::any_of(options_.agents_config.agents.begin(), options_.agents_config.agents.end(), [&](const auto& entry) {
    return entry.first == agent_type;
  });
}

bool NativeBlockingTaskSpawner::is_disabled_in_config(const std::string& agent_type) const {
  for(const auto& [id, override] : options_.agents_config.agents) {
    if(id == agent_type && override.enabled.has_value() && !*override.enabled) {
      return true;
    }
  }
  return false;
}

std::optional<std::vector<std::string>> NativeBlockingTaskSpawner::allowed_tools_for(
    const EffectiveSubagentDefinition& definition
) const {
  std::optional<std::vector<std::string>> profile_visible_tools;
  if(definition.runtime_profile == SubAgentRuntimeProfile::ReadOnly) {
    if(options_.parent_registry) {
      const auto visible = apply_runtime_profile_to_registry(*options_.parent_registry, definition.runtime_profile);
      std::vector<std::string> names;
      names.reserve(visible.size());
      for(const auto& tool : visible) {
        names.push_back(tool.name);
      }
      profile_visible_tools = std::move(names);
    } else {
      profile_visible_tools = read_only_runtime_tool_names();
    }
  }

  if(!definition.allowed_tools.has_value()) {
    return definition.runtime_profile == SubAgentRuntimeProfile::Full ? std::nullopt : profile_visible_tools;
  }

  if(definition.runtime_profile == SubAgentRuntimeProfile::Full) {
    return definition.allowed_tools;
  }

  std::vector<std::string> intersection;
  for(const auto& name : *definition.allowed_tools) {
    if(std::find(profile_visible_tools->begin(), profile_visible_tools->end(), name) != profile_visible_tools->end()) {
      intersection.push_back(name);
    }
  }
  return intersection;
}

std::vector<ChildRunInfo> NativeBlockingTaskSpawner::active_child_runs() const {
  const std::lock_guard<std::mutex> lock(child_runs_mutex_);
  std::vector<ChildRunInfo> runs;
  runs.reserve(active_child_runs_.size());
  for(const auto& [_, entry] : active_child_runs_) {
    runs.push_back(entry.info);
  }
  std::sort(runs.begin(), runs.end(), [](const auto& lhs, const auto& rhs) {
    return lhs.run_id < rhs.run_id;
  });
  return runs;
}

std::optional<ChildRunInfo> NativeBlockingTaskSpawner::active_child_run(const std::string& run_id) const {
  const std::lock_guard<std::mutex> lock(child_runs_mutex_);
  const auto it = active_child_runs_.find(run_id);
  if(it == active_child_runs_.end()) {
    return std::nullopt;
  }
  return it->second.info;
}

bool NativeBlockingTaskSpawner::cancel_child_run(const std::string& run_id) const {
  RunCancellationHandle handle;
  {
    const std::lock_guard<std::mutex> lock(child_runs_mutex_);
    const auto it = active_child_runs_.find(run_id);
    if(it == active_child_runs_.end()) {
      return false;
    }
    handle = it->second.cancel_handle;
  }

  handle.cancel();
  return true;
}

std::vector<ChildRunTerminalSummary> NativeBlockingTaskSpawner::child_terminal_summaries() const {
  const std::lock_guard<std::mutex> lock(child_runs_mutex_);
  return child_terminal_summaries_;
}

std::optional<ChildRunTerminalSummary> NativeBlockingTaskSpawner::child_terminal_summary(const std::string& run_id) const {
  const std::lock_guard<std::mutex> lock(child_runs_mutex_);
  const auto it = std::find_if(child_terminal_summaries_.begin(), child_terminal_summaries_.end(), [&](const auto& summary) {
    return summary.run_id == run_id;
  });
  if(it == child_terminal_summaries_.end()) {
    return std::nullopt;
  }
  return *it;
}

void NativeBlockingTaskSpawner::register_active_child_run(ChildRunInfo info, RunCancellationHandle handle) const {
  const std::lock_guard<std::mutex> lock(child_runs_mutex_);
  const auto run_id = info.run_id;
  active_child_runs_[run_id] = ActiveChildRunEntry{.info = std::move(info), .cancel_handle = std::move(handle)};
}

void NativeBlockingTaskSpawner::record_child_terminal_summary(ChildRunTerminalSummary summary) const {
  const std::lock_guard<std::mutex> lock(child_runs_mutex_);
  active_child_runs_.erase(summary.run_id);
  child_terminal_summaries_.push_back(std::move(summary));
  if(child_terminal_summaries_.size() > kMaxChildTerminalSummaries) {
    child_terminal_summaries_.erase(child_terminal_summaries_.begin());
  }
}

}  // namespace ava::orchestration
