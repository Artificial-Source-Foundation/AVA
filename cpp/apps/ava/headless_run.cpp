#include "headless_run.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

#include "ava/agent/agent.hpp"
#include "ava/config/paths.hpp"
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

void populate_queue_from_cli_inputs(const CliOptions&, ava::agent::MessageQueue&) {
  // Milestone 9 intentionally does not expose follow-up/later CLI queue flags yet.
  // Queue parity is deferred to a later milestone.
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

void persist_headless_metadata(
    ava::types::SessionRecord& session,
    const ava::orchestration::ResolvedRuntimeSelection& selection,
    const CliOptions& cli,
    const ava::agent::AgentRunResult& result,
    const std::string& run_id,
    ava::orchestration::SessionStartupKind startup_kind
) {
  session.metadata["headless"]["provider"] = selection.provider;
  session.metadata["headless"]["model"] = selection.model;
  session.metadata["headless"]["max_turns"] = selection.max_turns;
  session.metadata["headless"]["last_startup_kind"] = startup_kind_to_string(startup_kind);
  auto& last_run = session.metadata["headless"]["last_run"];
  last_run["reason"] = completion_reason_to_string(result.reason);
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

}  // namespace

int run_headless_blocking(const CliOptions& cli) {
  return run_headless_blocking(cli, nullptr);
}

int run_headless_blocking(const CliOptions& cli, ava::llm::ProviderPtr provider_override) {
  if(!cli.goal.has_value() || cli.goal->empty()) {
    throw std::invalid_argument("No goal provided. Usage: ava \"your goal here\"");
  }

  auto composition = ava::orchestration::compose_runtime(ava::orchestration::RuntimeCompositionRequest{
      .session_db_path = ava::config::app_db_path(),
      .workspace_root = std::filesystem::current_path(),
      .resume_latest = cli.resume,
      .session_id = cli.session_id,
      .selection = ava::orchestration::RuntimeSelectionOptions{
          .provider = cli.provider,
          .model = cli.model,
          .max_turns = cli.max_turns,
          .max_turns_explicit = cli.max_turns_explicit,
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
    std::cout << nlohmann::json{
                      {"type", "session_context"},
                      {"session_id", composition.session.id},
                      {"provider", composition.selection.provider},
                      {"model", composition.selection.model},
                  }
                      .dump()
               << "\n";
  } else {
    std::cout << "session=" << composition.session.id << " provider=" << composition.selection.provider
              << " model=" << composition.selection.model << "\n";
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

  persist_headless_metadata(composition.session, composition.selection, cli, result, run_lease.run_id, composition.startup_kind);
  composition.save_session();

  switch(result.reason) {
    case ava::agent::AgentCompletionReason::Completed:
      return 0;
    case ava::agent::AgentCompletionReason::Cancelled:
    case ava::agent::AgentCompletionReason::MaxTurns:
    case ava::agent::AgentCompletionReason::Stuck:
    case ava::agent::AgentCompletionReason::Error:
      return 2;
  }
  return 2;
}

}  // namespace ava::app
