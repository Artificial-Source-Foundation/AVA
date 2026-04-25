#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "ava/agent/runtime.hpp"
#include "ava/config/credentials.hpp"
#include "ava/llm/provider.hpp"
#include "ava/mcp/manager.hpp"
#include "ava/orchestration/interactive.hpp"
#include "ava/orchestration/run_controller.hpp"
#include "ava/session/session.hpp"
#include "ava/tools/registry.hpp"
#include "ava/types/session.hpp"

namespace ava::orchestration {

enum class SessionStartupKind {
  New,
  ContinueLatest,
  ContinueById,
};

struct RuntimeSelectionOptions {
  std::optional<std::string> provider;
  std::optional<std::string> model;
  std::size_t max_turns{16};
  bool max_turns_explicit{false};
};

struct ResolvedRuntimeSelection {
  std::string provider;
  std::string model;
  std::size_t max_turns{16};
};

struct ResolvedSessionStartup {
  ava::types::SessionRecord session;
  SessionStartupKind kind{SessionStartupKind::New};
};

[[nodiscard]] ResolvedSessionStartup resolve_startup_session(
    ava::session::SessionManager& manager,
    bool resume_latest,
    const std::optional<std::string>& session_id
);

[[nodiscard]] ResolvedRuntimeSelection resolve_runtime_selection(
    const RuntimeSelectionOptions& options,
    const ava::types::SessionRecord& session
);

struct RuntimeCompositionRequest {
  std::filesystem::path session_db_path;
  std::filesystem::path workspace_root{"."};
  bool resume_latest{false};
  std::optional<std::string> session_id;
  RuntimeSelectionOptions selection{};
  bool auto_approve{false};
  std::optional<std::vector<std::string>> allowed_tools;
  std::optional<std::string> system_prompt_preamble;
  InteractiveApprovalResolver approval_resolver;
  InteractiveQuestionResolver question_resolver;
  InteractivePlanResolver plan_resolver;
  ava::llm::ProviderPtr provider_override;
  std::function<ava::llm::ProviderPtr(const ResolvedRuntimeSelection&)> provider_factory;
  std::optional<ava::config::CredentialStore> credentials_override;
  bool load_global_mcp_config{false};
  std::optional<ava::mcp::McpConfig> mcp_config_override;
  ava::mcp::McpManager::TransportFactory mcp_transport_factory;
};

struct RuntimeComposition {
  std::unique_ptr<ava::session::SessionManager> sessions;
  ava::types::SessionRecord session;
  SessionStartupKind startup_kind{SessionStartupKind::New};
  ResolvedRuntimeSelection selection;
  ava::llm::ProviderPtr provider;
  std::shared_ptr<RunController> run_controller;
  std::shared_ptr<InteractiveBridge> interactive_bridge;
  std::shared_ptr<ava::tools::ToolRegistry> registry;
  std::shared_ptr<ava::mcp::McpManager> mcp_manager;
  ava::agent::MessageQueue queue;
  std::unique_ptr<ava::agent::AgentRuntime> runtime;

  void save_session() const;
};

[[nodiscard]] RuntimeComposition compose_runtime(RuntimeCompositionRequest request);

}  // namespace ava::orchestration
