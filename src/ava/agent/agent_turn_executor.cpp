#include "sys.h"
#include "ava/agent/agent_turn_executor_internal.h"
#include "ava/agent/stream_bridge.h"
#include "ava/agent/subagent_config.h"
#include "ava/agent/tool_timeline.h"

#include <string>
#include <utility>
#include <vector>

namespace ava::agent::detail {

AgentTurnExecutor::AgentTurnExecutor(AgentLoopOptions const& options, std::string const& user_message,
                                     std::vector<ava::session::ImageAttachmentRef> const& image_attachments, ava::session::SessionStore& store,
                                     ava::provider::Provider const& provider, ava::provider::Transport& transport,
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
      observed_transport_.emplace(transport_, ava::provider::TransportObservation{.observation = options_.observation, .context = trace_context_});
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
  tool_context_storage_.emplace(ava::tools::ToolContext{
      .workspace_dir = options_.workspace_dir,
      .spill_dir = store_.session_path().parent_path() / "spill",
      .mode = options_.mode,
      .permission_resolver = options_.permission_resolver,
      .command_deny_preflight = options_.command_deny_preflight,
      .permission_audit_sink = [session = &session_](ava::tools::PermissionAuditEvent const& event) -> ava::core::VoidResult {
        return session->append_permission_decision(event);
      },
      .progress_sink = [options = &options_](ava::tools::ToolProgressEvent const& event) -> ava::core::VoidResult {
        return publish_tool_progress(*options,
                                     ToolProgressEntry{.call_id = event.call_id, .name = event.tool_name, .text = event.text, .status = event.status});
      },
      .announce_execution_after_permission = options_.announce_execution_after_permission,
      .cancel_requested = options_.cancel_requested,
      .question_resolver = options_.question_resolver,
      .task_subagent_runner =
          options_.trace_context.parent_session_id.empty()
              ? ava::tools::TaskSubagentRunner([this](ava::tools::TaskSubagentRequest const& request) { return run_task_subagent(request); })
              : ava::tools::TaskSubagentRunner{},
      .subagent_coordinator = options_.subagent_coordinator,
      .subagents = subagents_,
      .redact_permission_audit_arguments = options_.redact_permission_audit_arguments,
      .require_explicit_file_permissions = options_.require_explicit_file_permissions,
      .anchor_set = options_.anchor_set,
      .ava_authority_roots = options_.ava_authority_roots,
      .exact_file_access = options_.exact_file_access,
      .command_executor = options_.command_executor,
      .lsp_diagnostics_provider = options_.lsp_diagnostics_provider,
      .plugin_global_plugins_dir = options_.plugin_global_plugins_dir,
      .plugin_project_plugins_dir = options_.plugin_project_plugins_dir,
      .plugin_enablement_file = options_.plugin_enablement_file,
      .include_project_plugins = options_.include_project_resources,
      .include_project_mcp_config = options_.include_project_resources,
      .session_mcp_config = options_.session_mcp_config,
      .exact_builtin_tool_names = options_.exact_builtin_tool_names,
      .require_descriptor_secure_workspace = options_.require_descriptor_secure_workspace,
      .include_project_skills = options_.include_project_resources,
      .session_id = store_.session_id(),
      .provider_id = options_.provider_id,
      .model_id = options_.model_id,
      .current_dir = options_.current_dir.empty() ? options_.workspace_dir : options_.current_dir,
      .tool_visibility = options_.tool_visibility});
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
  if (options_.exact_builtin_tool_names || options_.require_descriptor_secure_workspace)
  {
    auto strict_dispatcher = ToolDispatcher::create_strict(tool_context);
    if (!strict_dispatcher)
      return std::unexpected(std::move(strict_dispatcher.error()));
    dispatcher_storage_.emplace(std::move(*strict_dispatcher));
  }
  else
  {
    try
    {
      dispatcher_storage_.emplace(tool_context);
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
      dispatcher_storage_.emplace(tool_context);
    }
  }
  return {};
}

}  // namespace ava::agent::detail
