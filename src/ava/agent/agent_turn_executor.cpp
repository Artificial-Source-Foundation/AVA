#include "sys.h"
#include "ava/http/transport.h"
#include "ava/agent/agent_turn_executor_internal.h"
#include "ava/agent/stream_bridge.h"
#include "ava/agent/subagent_config.h"
#include "ava/agent/tool_timeline.h"

#include <expected>
#include <string>
#include <utility>
#include <vector>

namespace ava::agent::detail {

AgentTurnExecutor::AgentTurnExecutor(AgentLoopOptions const& options, std::string const& user_message,
                                     std::vector<ava::session::ImageAttachmentRef> const& image_attachments, ava::session::SessionStore& store,
                                     ava::provider::Provider const& provider, ava::http::Transport& transport,
                                     ava::observability::TraceContext const& trace_context)
    : options_(options),
      user_message_(user_message),
      image_attachments_(image_attachments),
      store_(store),
      provider_(provider),
      transport_(transport),
      trace_context_(trace_context),
      session_(options, store),
      effective_transport_(&transport)
{
  if (options_.observation && options_.observation->enabled())
  {
    try
    {
      observed_transport_.emplace(transport_, ava::http::TransportObservation{.observation = options_.observation, .context = trace_context_});
      effective_transport_ = &*observed_transport_;
    }
    catch (...)
    {
      options_.observation->account_external_failure();
    }
  }
}

ava::core::VoidResult AgentTurnExecutor::initialize_tools()
{
  auto const& tool_resources = options_.tool_resources;
  tool_context_storage_.emplace(
      ava::tools::ToolContext{.workspace_dir = options_.workspace_dir,
                              .spill_dir = store_.session_path().parent_path() / "spill",
                              .mode = options_.mode,
                              .permission_resolver = options_.permission_resolver,
                              .auto_allow_deny_preflight = options_.auto_allow_deny_preflight,
                              .permission_audit_sink = [session = &session_](ava::tools::PermissionAuditEvent const& event) -> ava::core::VoidResult {
                                return session->append_permission_decision(event);
                              },
                              .progress_sink = [options = &options_](ava::tools::ToolProgressEvent const& event) -> ava::core::VoidResult {
                                return publish_tool_progress(
                                    *options, ToolProgressEntry{.call_id = event.call_id, .name = event.tool_name, .text = event.text, .status = event.status});
                              },
                              .announce_execution_after_permission = options_.announce_execution_after_permission,
                              .cancel_requested = options_.cancel_requested,
                              .redact_permission_audit_arguments = options_.redact_permission_audit_arguments,
                              .require_explicit_file_permissions = options_.require_explicit_file_permissions,
                              .anchor_set = options_.anchor_set,
                              .ava_authority_roots = options_.ava_authority_roots,
                              .exact_file_access = options_.exact_file_access,
                              .command_executor = options_.command_executor,
                              .lsp_diagnostics_provider = tool_resources.lsp_diagnostics_provider,
                              .plugin_global_plugins_dir = tool_resources.plugin_global_plugins_dir,
                              .plugin_project_plugins_dir = tool_resources.plugin_project_plugins_dir,
                              .plugin_enablement_file = tool_resources.plugin_enablement_file,
                              .include_project_plugins = tool_resources.include_project_resources,
                              .include_plugin_tools = tool_resources.include_plugin_tools,
                              .include_project_mcp_config = tool_resources.include_project_resources,
                              .session_mcp_config = tool_resources.session_mcp_config,
                              .exact_builtin_tool_names = tool_resources.exact_builtin_tool_names,
                              .require_descriptor_secure_workspace = options_.require_descriptor_secure_workspace,
                              .include_project_skills = tool_resources.include_project_resources,
                              .session_id = store_.session_id(),
                              .provider_id = options_.provider_id,
                              .model_id = options_.model_id,
                              .current_dir = options_.current_dir.empty() ? options_.workspace_dir : options_.current_dir});
  // Build agent-owned dispatch services only after subagents_ is finalized so
  // task/job/question collaborators see the exact runtime catalog.
  ToolDispatchServices dispatch_services{
      .question_resolver = options_.question_resolver,
      .task_subagent_runner = options_.trace_context.parent_session_id.empty()
                                  ? TaskSubagentRunner([this](TaskSubagentRequest const& request) { return run_task_subagent(request); })
                                  : TaskSubagentRunner{},
      .subagent_coordinator = options_.subagent_coordinator,
      .subagents = subagents_,
  };
  auto& tool_context = *tool_context_storage_;
  if (options_.observation && options_.observation->enabled())
  {
    try
    {
      tool_context.observation = options_.observation;
      tool_context.trace_context = trace_context_;
    }
    catch (...)
    {
      tool_context.observation.reset();
      tool_context.trace_context = {};
      options_.observation->account_external_failure();
    }
  }
  if (tool_resources.exact_builtin_tool_names || options_.require_descriptor_secure_workspace)
  {
    auto strict_dispatcher = ToolDispatcher::create_strict(tool_context, dispatch_services, options_.tool_visibility);
    if (!strict_dispatcher)
      return std::unexpected(std::move(strict_dispatcher.error()));
    dispatcher_storage_.emplace(std::move(*strict_dispatcher));
  }
  else
  {
    try
    {
      dispatcher_storage_.emplace(tool_context, dispatch_services, options_.tool_visibility);
    }
    catch (...)
    {
      // ToolDispatcher owns a copy of ToolContext. If only that observation
      // setup cannot be prepared, retry with the exact baseline context.
      if (!tool_context.observation)
        throw;
      auto observation = std::move(tool_context.observation);
      tool_context.trace_context = {};
      observation->account_external_failure();
      dispatcher_storage_.emplace(tool_context, dispatch_services, options_.tool_visibility);
    }
  }
  return {};
}

ava::core::Result<AgentLoopResult> AgentTurnExecutor::run()
{
  auto finalized_ids_result = session_.persisted_provider_tool_call_ids();
  if (!finalized_ids_result)
    return std::unexpected(std::move(finalized_ids_result.error()));
  finalized_provider_tool_call_ids_ = std::move(*finalized_ids_result);

  if (auto not_canceled = session_.check_canceled("before_turn_start"); !not_canceled)
    return std::unexpected(std::move(not_canceled.error()));
  pre_turn_compacted_ = false;
  if (options_.compact_context)
  {
    auto compacted = compact_context("auto");
    if (!compacted)
      return std::unexpected(std::move(compacted.error()));
    pre_turn_compacted_ = *compacted;
    if (auto not_canceled = session_.check_canceled("after_pre_turn_auto_compaction"); !not_canceled)
      return std::unexpected(std::move(not_canceled.error()));
  }
  if (auto phase = publish_phase(RunPhase::BuildingContext); !phase)
    return std::unexpected(std::move(phase.error()));
  if (auto appended = append_active_turn_user_message(user_message_, image_attachments_); !appended)
    return std::unexpected(appended.error());

  subagents_ = options_.subagents.empty() ? builtin_subagents() : options_.subagents;
  if (auto initialized = initialize_tools(); !initialized)
    return std::unexpected(std::move(initialized.error()));

  while (true)
  {
    auto provider_turn = request_provider_turn();
    if (!provider_turn)
      return std::unexpected(std::move(provider_turn.error()));
    auto const& turn = provider_turn->assistant_turn;
    if (provider_turn->disposition == ProviderTurnDisposition::TerminalCancelled)
    {
      result_.final_text = turn.text;
      result_.tool_iterations = tool_iterations_;
      result_.outcome = ava::core::RuntimeTerminalOutcome::Cancelled;
      return result_;
    }

    // Declared after provider_turn so its borrowed ParsedAssistantTurn remains
    // alive until best-effort committed-binding closure has completed.
    PendingCommittedToolResults pending_tool_results(session_, turn);
    if (auto persisted = persist_assistant_turn(*provider_turn, pending_tool_results); !persisted)
      return std::unexpected(std::move(persisted.error()));

    if (turn.tool_calls.empty())
    {
      result_.final_text = turn.text;
      result_.tool_iterations = tool_iterations_;
      switch (*turn.finish_reason)
      {
        case ava::provider::ProviderFinishReason::Completed:
          result_.outcome = ava::core::RuntimeTerminalOutcome::Completed;
          break;
        case ava::provider::ProviderFinishReason::MaxTokens:
          result_.outcome = ava::core::RuntimeTerminalOutcome::MaxTokens;
          break;
        case ava::provider::ProviderFinishReason::Refusal:
          result_.outcome = ava::core::RuntimeTerminalOutcome::Refusal;
          break;
        case ava::provider::ProviderFinishReason::Cancelled:
          result_.outcome = ava::core::RuntimeTerminalOutcome::Cancelled;
          break;
        case ava::provider::ProviderFinishReason::ToolCalls:
        case ava::provider::ProviderFinishReason::Error:
          result_.outcome = ava::core::RuntimeTerminalOutcome::Error;
          break;
      }
      return result_;
    }

    if (auto phase = publish_phase(RunPhase::PreparingTools); !phase)
      return std::unexpected(std::move(phase.error()));
    if (auto executed = execute_tools(turn, pending_tool_results); !executed)
      return std::unexpected(std::move(executed.error()));
    if (auto phase = publish_phase(RunPhase::SettlingTools); !phase)
      return std::unexpected(std::move(phase.error()));
    ++tool_iterations_;
    result_.tool_iterations = tool_iterations_;
    if (tool_iterations_ >= options_.max_tool_iterations)
    {
      if (auto phase = publish_phase(RunPhase::Completing); !phase)
        return std::unexpected(std::move(phase.error()));
      result_.outcome = ava::core::RuntimeTerminalOutcome::MaxTurnRequests;
      return result_;
    }
    if (auto phase = publish_phase(RunPhase::AwaitingProvider); !phase)
      return std::unexpected(std::move(phase.error()));
  }
}

}  // namespace ava::agent::detail
