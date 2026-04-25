#include "headless_run.hpp"

#include <filesystem>
#include <iostream>
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

#include "ava/agent/agent.hpp"
#include "ava/config/paths.hpp"
#include "ava/config/trust.hpp"
#include "ava/orchestration/composition.hpp"
#include "events.hpp"
#include "signal_cancel.hpp"

namespace ava::app {
namespace {

class HeadlessSignalCancelScope {
 public:
  HeadlessSignalCancelScope() {
    reset_headless_signal_cancel();
    install_headless_signal_cancel_handlers();
  }

  HeadlessSignalCancelScope(const HeadlessSignalCancelScope&) = delete;
  HeadlessSignalCancelScope& operator=(const HeadlessSignalCancelScope&) = delete;
  HeadlessSignalCancelScope(HeadlessSignalCancelScope&&) = delete;
  HeadlessSignalCancelScope& operator=(HeadlessSignalCancelScope&&) = delete;

  ~HeadlessSignalCancelScope() {
    restore_headless_signal_cancel_handlers();
  }
};

void populate_queue_from_cli_inputs(const CliOptions& cli, ava::agent::MessageQueue& queue) {
  for(const auto& message : cli.follow_up_messages) {
    queue.enqueue(ava::agent::QueuedMessage{.text = message, .tier = ava::types::MessageTier::follow_up()});
  }
  for(const auto& message : cli.post_complete_messages) {
    queue.enqueue(ava::agent::QueuedMessage{.text = message, .tier = ava::types::MessageTier::post_complete()});
  }
  for(const auto& message : cli.post_complete_group_messages) {
    queue.enqueue(ava::agent::QueuedMessage{
        .text = message.text,
        .tier = ava::types::MessageTier::post_complete(message.group),
    });
  }
}

[[nodiscard]] std::string startup_kind_to_string(ava::orchestration::SessionStartupKind kind) {
  switch(kind) {
    case ava::orchestration::SessionStartupKind::New:
      return "new";
    case ava::orchestration::SessionStartupKind::ContinueLatest:
      return "continue_latest";
    case ava::orchestration::SessionStartupKind::ContinueById:
      return "continue_by_id";
  }
  return "new";
}

void persist_headless_metadata(
    ava::types::SessionRecord& session,
    const ava::orchestration::ResolvedRuntimeSelection& selection,
    const CliOptions& cli,
    const ava::agent::AgentRunResult& result,
    const std::string& run_id,
    ava::orchestration::SessionStartupKind startup_kind,
    const std::filesystem::path& workspace_root
) {
  session.metadata["headless"]["provider"] = selection.provider;
  session.metadata["headless"]["model"] = selection.model;
  if(selection.agent.has_value()) {
    session.metadata["headless"]["agent"] = *selection.agent;
  } else {
    session.metadata["headless"].erase("agent");
  }
  session.metadata["headless"]["max_turns"] = selection.max_turns;
  session.metadata["headless"]["last_startup_kind"] = startup_kind_to_string(startup_kind);
  session.metadata["headless"]["workspace_root"] = workspace_root.string();
  auto& last_run = session.metadata["headless"]["last_run"];
  last_run["reason"] = ava::agent::completion_reason_to_string(result.reason);
  last_run["run_id"] = run_id;
  last_run["turns_used"] = result.turns_used;
  last_run["json"] = cli.json;
  last_run["auto_approve"] = cli.auto_approve;
  if(result.error.has_value()) {
    last_run["error"] = *result.error;
  } else {
    last_run.erase("error");
  }
}

[[nodiscard]] std::filesystem::path resolve_workspace_root(const CliOptions& cli) {
  std::filesystem::path root = cli.cwd.value_or(std::filesystem::current_path());
  std::error_code ec;
  const auto canonical = std::filesystem::weakly_canonical(root, ec);
  if(!ec) {
    root = canonical;
  }
  if(!std::filesystem::exists(root) || !std::filesystem::is_directory(root)) {
    throw std::invalid_argument("--cwd must point to an existing directory: " + root.string());
  }
  return root;
}

}  // namespace

int run_headless_blocking(const CliOptions& cli) {
  return run_headless_blocking(cli, nullptr);
}

int run_headless_blocking(const CliOptions& cli, ava::llm::ProviderPtr provider_override) {
  if(!cli.goal.has_value() || cli.goal->empty()) {
    throw std::invalid_argument("No goal provided. Usage: ava \"your goal here\"");
  }

  const auto workspace_root = resolve_workspace_root(cli);
  if(cli.trust) {
    ava::config::trust_project(workspace_root);
  }

  auto composition = ava::orchestration::compose_runtime(ava::orchestration::RuntimeCompositionRequest{
      .session_db_path = ava::config::app_db_path(),
      .workspace_root = workspace_root,
      .resume_latest = cli.resume,
      .session_id = cli.session_id,
      .selection = ava::orchestration::RuntimeSelectionOptions{
          .provider = cli.provider,
          .model = cli.model,
          .agent = cli.agent,
          .max_turns = cli.max_turns,
          .max_turns_explicit = cli.max_turns_explicit,
          .max_budget_usd = cli.max_budget_usd,
      },
      .auto_approve = cli.auto_approve,
      .allowed_tools = std::nullopt,
      .system_prompt_preamble = std::nullopt,
      .provider_override = std::move(provider_override),
      .provider_factory = nullptr,
      .credentials_override = std::nullopt,
      .load_global_mcp_config = true,
  });

  populate_queue_from_cli_inputs(cli, composition.queue);

  if(cli.json) {
    nlohmann::json context{
        {"type", "session_context"},
        {"session_id", composition.session.id},
        {"provider", composition.selection.provider},
        {"model", composition.selection.model},
        {"workspace_root", workspace_root.string()},
    };
    if(composition.selection.agent.has_value()) {
      context["agent"] = *composition.selection.agent;
    }
    std::cout << context.dump() << "\n";
  } else {
    std::cout << "session=" << composition.session.id << " provider=" << composition.selection.provider
              << " model=" << composition.selection.model;
    if(composition.selection.agent.has_value()) {
      std::cout << " agent=" << *composition.selection.agent;
    }
    std::cout << " workspace=" << workspace_root.string() << "\n";
  }

  const auto run_lease = composition.run_controller->begin_run();
  composition.interactive_bridge->set_run_id(run_lease.run_id);
  const HeadlessSignalCancelScope signal_cancel_scope;

  ava::agent::AgentRunResult result;
  try {
    result = composition.runtime->run(
        composition.session,
        ava::agent::AgentRunInput{
            .goal = *cli.goal,
            .queue = &composition.queue,
            .run_id = run_lease.run_id,
            .is_cancelled = [&] {
              return run_lease.token.is_cancelled() || headless_signal_cancel_requested();
            },
            .stream = true,
        },
        [&](const ava::agent::AgentEvent& event) {
          if(event.kind == ava::agent::AgentEventKind::Checkpoint) {
            composition.save_session();
          }
          if(cli.json) {
            std::cout << headless_event_to_ndjson(event).dump() << "\n";
            return;
          }
          print_headless_event_text(event);
        }
    );
  } catch(...) {
    composition.interactive_bridge->set_run_id(std::nullopt);
    throw;
  }
  composition.interactive_bridge->set_run_id(std::nullopt);

  if(result.error.has_value() && result.error->find("requires approval") != std::string::npos) {
    const std::string message =
        *result.error + " (non-interactive headless mode cannot prompt; rerun with --auto-approve if this action is trusted)";
    if(!cli.json) {
      std::cerr << message << "\n";
    }
  }

  persist_headless_metadata(
      composition.session,
      composition.selection,
      cli,
      result,
      run_lease.run_id,
      composition.startup_kind,
      workspace_root
  );
  composition.save_session();

  switch(result.reason) {
    case ava::agent::AgentCompletionReason::Completed:
      return 0;
    case ava::agent::AgentCompletionReason::Cancelled:
    case ava::agent::AgentCompletionReason::MaxTurns:
    case ava::agent::AgentCompletionReason::Stuck:
    case ava::agent::AgentCompletionReason::BudgetExceeded:
    case ava::agent::AgentCompletionReason::Error:
      return 2;
  }
  return 2;
}

}  // namespace ava::app
