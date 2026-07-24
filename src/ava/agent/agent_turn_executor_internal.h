#pragma once

#include "ava/agent/agent_loop.h"
#include "ava/agent/agent_loop_session.h"
#include "ava/agent/message_builder.h"
#include "ava/agent/tool_dispatcher.h"
#include "ava/tools/file_tools.h"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>
#include "debug.h"

namespace ava::agent::detail {

struct ActiveTurnUserMessage
{
  std::string id;
  std::string text;
  std::vector<ava::session::ImageAttachmentRef> image_attachments;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

class AgentTurnSession final
{
 public:
  AgentTurnSession(AgentLoopOptions const& options, ava::session::SessionStore& store) noexcept;

  [[nodiscard]] bool is_canceled() const;
  [[nodiscard]] ava::core::VoidResult check_canceled(std::string_view boundary);
  [[nodiscard]] ava::core::Result<std::string> append_user_message(std::string const& text, std::vector<ava::session::ImageAttachmentRef> const& attachments);
  [[nodiscard]] ava::core::VoidResult append_replay_user_message(ActiveTurnUserMessage const& message);
  [[nodiscard]] ava::core::Result<PersistedAssistantTurn> append_assistant_turn(ParsedAssistantTurn const& turn, ava::provider::TokenUsage const& usage,
                                                                                std::optional<long double> const& cost_usd);
  [[nodiscard]] ava::core::VoidResult append_tool_result(ToolDispatchResult const& dispatch_result,
                                                         std::optional<std::string_view> assistant_output_entry_id = std::nullopt);
  [[nodiscard]] ava::core::VoidResult append_permission_decision(ava::tools::PermissionAuditEvent const& event);
  [[nodiscard]] ava::core::VoidResult append_error(ava::core::Error const& error);
  [[nodiscard]] ava::core::Result<BuiltProviderMessages> build_messages(MessageBuildOptions options);
  [[nodiscard]] ava::core::Result<std::unordered_set<std::string>> persisted_provider_tool_call_ids();
  [[nodiscard]] ava::core::VoidResult attach_verified_image_payloads(ava::provider::ProviderRequest& request) const;

  // Borrows options containing credentials and callbacks; never generate debug output for this holder.
  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT

 private:
  AgentLoopOptions const& options_;
  ava::session::SessionStore& store_;
};

class AgentTurnExecutor final
{
 public:
  AgentTurnExecutor(AgentLoopOptions const& options, std::string const& user_message, std::vector<ava::session::ImageAttachmentRef> const& image_attachments,
                    ava::session::SessionStore& store, ava::provider::Provider const& provider, ava::provider::Transport& transport,
                    ava::observability::TraceContext const& trace_context);

  [[nodiscard]] ava::core::Result<AgentLoopResult> run();

  // Borrows provider credentials, callbacks, transport, and session authority for one synchronous turn.
  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT

 private:
  [[nodiscard]] ava::core::VoidResult publish_phase(RunPhase phase) const;
  [[nodiscard]] MessageBuildOptions message_build_options() const;
  [[nodiscard]] ava::core::Result<BuiltProviderMessages> build_messages();
  [[nodiscard]] std::vector<std::string> replayable_active_turn_texts() const;
  [[nodiscard]] ava::core::Result<bool> compact_context(std::string_view trigger);
  [[nodiscard]] ava::core::VoidResult append_active_turn_user_message(std::string const& text,
                                                                      std::vector<ava::session::ImageAttachmentRef> const& attachments);
  [[nodiscard]] ava::core::VoidResult replay_active_turn_user_messages();
  [[nodiscard]] ava::core::Result<bool> prepare_context_overflow_retry(ava::core::Error const& error);
  [[nodiscard]] ava::core::VoidResult initialize_tools();
  [[nodiscard]] ava::core::Result<ava::tools::TaskSubagentResult> run_task_subagent(ava::tools::TaskSubagentRequest const& request);

  AgentLoopOptions const& options_;
  std::string const& user_message_;
  std::vector<ava::session::ImageAttachmentRef> const& image_attachments_;
  ava::session::SessionStore& store_;
  ava::provider::Provider const& provider_;
  ava::provider::Transport& transport_;
  ava::observability::TraceContext const& trace_context_;
  AgentTurnSession session_;
  std::optional<ava::provider::ObservedTransport> observed_transport_;
  ava::provider::Transport* effective_transport_ = nullptr;
  std::vector<ActiveTurnUserMessage> active_turn_user_messages_;
  bool context_overflow_retry_used_ = false;
  bool skip_auto_compaction_after_overflow_retry_ = false;
  bool pre_turn_compacted_ = false;
  std::unordered_set<std::string> finalized_provider_tool_call_ids_;
  AgentLoopResult result_;
  std::vector<SubagentDefinition> subagents_;
  std::optional<ava::tools::ToolContext> tool_context_storage_;
  std::optional<ToolDispatcher> dispatcher_storage_;
  std::size_t tool_iterations_ = 0;
  bool accumulated_cost_known_ = true;
};

[[nodiscard]] std::pair<std::vector<std::filesystem::path>, bool> bounded_deduplicated_authority_roots(std::vector<std::filesystem::path> roots);

}  // namespace ava::agent::detail
