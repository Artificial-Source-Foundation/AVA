#include "headless_run.hpp"

#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

#include "agent_config.hpp"
#include "ava/agent/agent.hpp"
#include "ava/config/paths.hpp"
#include "ava/session/session.hpp"
#include "ava/tools/tools.hpp"
#include "events.hpp"
#include "session_resolver.hpp"

namespace ava::app {
namespace {

class HeadlessApprovalBridge final : public ava::tools::ApprovalBridge {
 public:
  [[nodiscard]] ava::tools::ToolApproval request_approval(
      const ava::types::ToolCall&,
      const ava::tools::PermissionInspection&
  ) const override {
    return ava::tools::ToolApproval{.kind = ava::tools::ToolApprovalKind::Allowed};
  }
};

void populate_queue_from_cli_inputs(const CliOptions&, ava::agent::MessageQueue&) {
  // Milestone 9 intentionally does not expose follow-up/later CLI queue flags yet.
  // Queue parity is deferred to a later milestone.
}

[[nodiscard]] std::string startup_kind_to_string(SessionStartupKind kind) {
  switch(kind) {
    case SessionStartupKind::New:
      return "new";
    case SessionStartupKind::ContinueLatest:
      return "continue_latest";
    case SessionStartupKind::ContinueById:
      return "continue_by_id";
  }
  return "new";
}

[[nodiscard]] std::string completion_reason_to_string(ava::agent::AgentCompletionReason reason) {
  switch(reason) {
    case ava::agent::AgentCompletionReason::Completed:
      return "completed";
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
    const ResolvedAgentSelection& selection,
    const CliOptions& cli,
    const ava::agent::AgentRunResult& result,
    SessionStartupKind startup_kind
) {
  session.metadata["headless"]["provider"] = selection.provider;
  session.metadata["headless"]["model"] = selection.model;
  session.metadata["headless"]["max_turns"] = selection.max_turns;
  session.metadata["headless"]["last_startup_kind"] = startup_kind_to_string(startup_kind);
  session.metadata["headless"]["last_run"]["reason"] = completion_reason_to_string(result.reason);
  session.metadata["headless"]["last_run"]["turns_used"] = result.turns_used;
  session.metadata["headless"]["last_run"]["json"] = cli.json;
  session.metadata["headless"]["last_run"]["auto_approve"] = cli.auto_approve;
  if(result.error.has_value()) {
    session.metadata["headless"]["last_run"]["error"] = *result.error;
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

  ava::session::SessionManager sessions(ava::config::app_db_path());
  auto startup = resolve_startup_session(sessions, cli.resume, cli.session_id);

  const auto selection = resolve_agent_selection(cli, startup.session);
  auto provider = std::move(provider_override);
  if(!provider) {
    const auto credentials = load_credentials_for_run();
    provider = build_provider_for_run(selection, credentials);
  }

  ava::tools::ToolRegistry registry;
  ava::tools::register_default_tools(registry, std::filesystem::current_path());
  std::shared_ptr<ava::tools::ApprovalBridge> approval_bridge;
  if(cli.auto_approve) {
    approval_bridge = std::make_shared<HeadlessApprovalBridge>();
  }
  registry.add_middleware(std::make_shared<ava::tools::PermissionMiddleware>(
      std::make_shared<ava::tools::DefaultHeadlessPermissionInspector>(),
      std::move(approval_bridge)
  ));

  ava::agent::MessageQueue queue;
  populate_queue_from_cli_inputs(cli, queue);

  ava::agent::AgentRuntime runtime(*provider, registry, ava::agent::AgentConfig{.max_turns = selection.max_turns});

  if(cli.json) {
    std::cout << nlohmann::json{
                     {"type", "session_context"},
                     {"session_id", startup.session.id},
                     {"provider", selection.provider},
                     {"model", selection.model},
                 }
                     .dump()
              << "\n";
  } else {
    std::cout << "session=" << startup.session.id << " provider=" << selection.provider << " model=" << selection.model << "\n";
  }

  auto result = runtime.run(
      startup.session,
      ava::agent::AgentRunInput{.goal = *cli.goal, .queue = &queue},
      [&](const ava::agent::AgentEvent& event) {
        if(cli.json) {
          std::cout << headless_event_to_ndjson(event).dump() << "\n";
          return;
        }
        print_headless_event_text(event);
      }
  );

  if(result.error.has_value() && result.error->find("requires approval") != std::string::npos) {
    const std::string message =
        *result.error + " (non-interactive headless mode cannot prompt; rerun with --auto-approve for this M9 lane)";
    if(!cli.json) {
      std::cerr << message << "\n";
    }
  }

  persist_headless_metadata(startup.session, selection, cli, result, startup.kind);
  sessions.save(startup.session);

  switch(result.reason) {
    case ava::agent::AgentCompletionReason::Completed:
      return 0;
    case ava::agent::AgentCompletionReason::MaxTurns:
    case ava::agent::AgentCompletionReason::Stuck:
    case ava::agent::AgentCompletionReason::Error:
      return 2;
  }
  return 2;
}

}  // namespace ava::app
