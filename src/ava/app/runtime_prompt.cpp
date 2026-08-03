#include "sys.h"
#include "plugin_event_hooks.h"
#include "runtime/ExtensionResourcePolicy.h"
#include "runtime/Session.h"
#include "runtime_compaction.h"
#include "runtime_event_adapters.h"
#include "runtime_prompt.h"
#include "runtime_prompt_file_references.h"
#include "runtime_reasoning.h"
#include "runtime_retry.h"
#include "runtime_run_outcomes.h"
#include "session_title_coordinator.h"
#include "subagent_delivery_manager.h"
#include "ava/diagnostics/runtime_diagnostics.h"
#include "ava/event/events.h"
#include "ava/http/curl_transport.h"
#include "ava/agent/agent_loop_session.h"
#include "ava/agent/subagent_config.h"
#include "ava/session/session_store.h"
#include "ava/permissions/permission_rules.h"
#include "ava/provider/registry.h"
#include "ava/lsp/configured_provider.h"
#include "ava/core/error.h"
#include "ava/core/ids.h"

#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ava::app {
namespace {

ava::event::RuntimeEventMetadata runtime_event_metadata_2(runtime::Session const& session, std::mutex* mutex)
{
  auto build = [&] {
    return ava::event::RuntimeEventMetadata{
        .timestamp = ava::session::now_timestamp(),
        .session_id = session.store.session_id(),
    };
  };
  if (!mutex)
    return build();
  std::lock_guard lock(*mutex);
  return build();
}

// SessionStart reads mode/provider/model under the same lock as metadata so the
// published snapshot stays atomic with the timestamp/session id capture.
ava::event::RuntimeEvent session_start_event(runtime::Session const& session, std::mutex* mutex)
{
  auto build = [&] {
    return ava::event::RuntimeEvent{ava::event::RuntimeEventMetadata{
                                        .timestamp = ava::session::now_timestamp(),
                                        .session_id = session.store.session_id(),
                                    },
                                    ava::event::SessionStartEvent{.payload = ava::event::SessionPayload{
                                                                      .mode = session.mode(),
                                                                      .provider = session.model().provider_id,
                                                                      .model = session.model().model_id,
                                                                  }}};
  };
  if (!mutex)
    return build();
  std::lock_guard lock(*mutex);
  return build();
}

}  // namespace

ava::core::Result<runtime::PromptState> select_runtime_prompt_state(runtime::Session const& session, ava::agent::Mode mode)
{
  return runtime::load_runtime_prompt_state(session.paths(), session.model(), mode, session.workspace_dir(), session.current_dir(),
                                            project_resources_trusted(session.project_trust()), session.prompt_overrides());
}

ava::core::Error offline_provider_error(std::string_view action)
{
  auto error = ava::core::Error(ava::core::ErrorCategory::PermissionDenied, "offline mode is enabled; provider model calls are disabled");
  if (!action.empty())
    error.with_context("action", std::string(action));
  error.with_context("hint", "rerun without --offline to send prompts to the provider");
  return error;
}

ava::core::Result<ava::agent::AgentLoopResult> run_prompt(runtime::session_ts& unlocked_session, std::string const& user_message, ava::provider::Provider const& provider,
                                                          ava::http::Transport& transport, runtime::RunOptions const& options)
{
  CRITICAL_AREA_BEGIN_R(session);

  if (session_r->is_offline() || options.offline)
    return std::unexpected(offline_provider_error("prompt"));
  if (!session_r->run_controller())
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "runtime session controller is unavailable"));
  auto const request_id = options.request_id.value_or(ava::core::make_id("request"));
  auto const admission = session_r->run_controller()->inspect_admission(RunRequest{.request_id = request_id});
  if (admission == AdmissionDisposition::JoinExistingOutcome)
  {
    auto joined = session_r->run_controller()->wait_outcome(request_id);
    if (!joined)
      return std::unexpected(std::move(joined.error()));
    if (joined->reason == StopReason::PersistenceError && joined->error)
    {
      if (session_r->diagnostics())
        session_r->diagnostics()->record_terminal_failure(ava::diagnostics::RuntimeFailureClass::Session, *joined->error);
      return std::unexpected(*joined->error);
    }
    return ava::agent::AgentLoopResult{.final_text = {},
                                       .usage = std::nullopt,
                                       .cost_usd = std::nullopt,
                                       .provider_iterations = 0,
                                       .tool_calls = 0,
                                       .initial_context_messages = 0,
                                       .used_compacted_context = false,
                                       .tool_iterations = 0,
                                       .outcome = runtime_outcome_for_stop_reason(joined->reason),
                                       .tool_timeline = {}};
  }
  if (admission == AdmissionDisposition::RejectDifferentRequest)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "session already has an active run for a different request");
    error.with_context("request_id", request_id);
    return std::unexpected(std::move(error));
  }
  auto admitted = session_r->run_controller()->admit(RunRequest{.request_id = request_id});
  if (!admitted)
    return std::unexpected(std::move(admitted.error()));

  CRITICAL_AREA_END_R(session);

  return run_admitted_prompt(unlocked_session, user_message, provider, transport, options, std::move(*admitted));
}

ava::core::Result<ava::agent::AgentLoopResult> run_admitted_prompt(runtime::session_ts& unlocked_session, std::string const& user_message,
                                                                   ava::provider::Provider const& provider, ava::http::Transport& transport,
                                                                   runtime::RunOptions const& options, ActiveRunGuard guard)
{
  CRITICAL_AREA_BEGIN_W(session);

  std::optional<std::string_view> launch_reasoning_level = std::nullopt;
  if (session_w->reasoning())
    launch_reasoning_level = session_w->reasoning()->level;
  // Normalize immutable private presentation before parent/coordinator locks.
  // Model/provider ids and provider-specific reasoning fields never enter it.
  auto const subagent_launch_display =
      ava::agent::SubagentLaunchDisplay::normalized(ava::config::proven_configured_model_display_name(session_w->model()), launch_reasoning_level);

  auto fail_run = [&guard, &session_w](ava::core::Error error) -> ava::core::Result<ava::agent::AgentLoopResult> {
    if (session_w->diagnostics())
      if (auto failure_class = diagnostic_failure_class(error))
        session_w->diagnostics()->record_terminal_failure(*failure_class, error);
    auto completed = guard.complete(RunOutcome{.run_id = {}, .reason = outcome_reason_for_error(error), .error = error});
    if (completed && completed->reason == StopReason::PersistenceError && completed->error)
    {
      if (session_w->diagnostics())
        session_w->diagnostics()->record_terminal_failure(ava::diagnostics::RuntimeFailureClass::Session, *completed->error);
      return std::unexpected(*completed->error);
    }
    return std::unexpected(std::move(error));
  };
  struct ParentRefresh final
  {
    std::shared_ptr<SubagentDeliveryManager> manager;
    std::string session_id;
    std::optional<SubagentDeliveryManager::CapsuleGeneration> generation = std::nullopt;
    ~ParentRefresh()
    {
      if (manager && generation)
        manager->release_parent_if_unused(session_id, *generation);
    }
  } refresh{options.synthetic_subagent_delivery ? nullptr : session_w->subagent_delivery_manager(), session_w->store.session_id()};

  if (refresh.manager)
  {
    CRITICAL_AREA_END_W(session);
    auto retained = refresh.manager->refresh_parent(unlocked_session, options);
    CRITICAL_AREA_CONTINUE_W(session);

    if (!retained)
      return fail_run(std::move(retained.error()));
    refresh.generation = *retained;
  }

  if (session_w->is_offline() || options.offline)
    return fail_run(offline_provider_error("prompt"));
  if (!session_w->run_controller())
    return fail_run(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "runtime session controller is unavailable"));
  auto const subagent_launch_correlation_id = session_w->run_controller()->snapshot().run_id;
  if (!guard.active())
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "runtime prompt admission is inactive"));
  auto session_read_authority = session_w->read_authority_1();
  if (!session_read_authority)
    return fail_run(std::move(session_read_authority.error()));
  if (auto transitioned = guard.transition(RunPhase::BuildingContext); !transitioned)
    return fail_run(std::move(transitioned.error()));

  // Hook permission audits can overlap provider/tool work, so use this run's
  // immutable route rather than the legacy direct store callback. Isolated
  // runs bypass ambient plugin event hooks entirely.
  auto append_route = guard.append_route();
  auto append_batch_route = guard.append_batch_route();
  ava::event::RuntimeEventSink event_sink = options.event_sink;
  if (!options.isolate_ambient_extensions)
  {
    auto plugin_observer_options = plugin_event_observer_options(*session_w, options.permission_resolver, options.session_mutex);
    plugin_observer_options.permission_audit_sink = [append_route](ava::tools::PermissionAuditEvent const& event) {
      return ava::agent::append_permission_decision(append_route, event);
    };
    plugin_observer_options.cancel_requested = options.cancel_requested;
    event_sink = make_plugin_event_observer_sink(std::move(plugin_observer_options), options.event_sink);
  }
  auto runtime_options = options;
  if (session_w->diagnostics())
  {
    auto production_observation = session_w->diagnostics()->observation();
    if (production_observation && production_observation->enabled())
      runtime_options.observation = std::move(production_observation);
  }
  runtime_options.active_append_route = append_route;
  runtime_options.active_append_batch_route = append_batch_route;
  auto const caller_cancel_requested = runtime_options.cancel_requested;
  runtime_options.cancel_requested = [guard_token = guard.stop_token(), caller_cancel_requested] {
    return guard_token.stop_requested() || (caller_cancel_requested && caller_cancel_requested());
  };
  runtime_options.event_sink = event_sink;
  auto take_steering_messages = runtime_options.take_steering_messages;
  runtime_options.take_steering_messages = [&session_w, take_steering_messages]() -> ava::core::Result<std::vector<std::string>> {
    if (!take_steering_messages)
      return std::vector<std::string>{};
    auto messages = take_steering_messages();
    if (!messages)
      return std::unexpected(std::move(messages.error()));
    auto const run_id = session_w->run_controller()->snapshot().run_id;
    for (auto const& message : *messages)
    {
      // Frontends retain their own bounded visible queues. Controller overflow
      // rejects only the extra steering item at this adapter boundary; it must
      // not abort an otherwise healthy run or discard already admitted input.
      static_cast<void>(session_w->run_controller()->wake(RunCommand{.kind = RunCommand::Kind::Steering, .correlation_id = run_id, .message = message}));
    }
    auto commands = session_w->run_controller()->take_commands(run_id);
    if (!commands)
      return std::unexpected(std::move(commands.error()));
    std::vector<std::string> accepted;
    accepted.reserve(commands->size());
    for (auto& command : *commands)
      if (command.kind == RunCommand::Kind::Steering)
        accepted.push_back(std::move(command.message));
    return accepted;
  };
  if (runtime_options.observation && runtime_options.observation->enabled())
  {
    if (runtime_options.trace_context.run_id.empty())
      runtime_options.trace_context.run_id = runtime_options.observation->next_id("run");
    if (runtime_options.trace_context.turn_id.empty())
      runtime_options.trace_context.turn_id = runtime_options.observation->next_id("turn");
    if (runtime_options.trace_context.session_id.empty())
      runtime_options.trace_context.session_id = session_w->store.session_id();
    if (runtime_options.trace_context.provider_id.empty())
      runtime_options.trace_context.provider_id = session_w->model().provider_id;
  }
  std::optional<ava::http::RetryTransport> retry_transport;
  ava::http::Transport* runtime_transport = &transport;
  if (runtime_options.enable_transport_retries)
  {
    retry_transport.emplace(transport, runtime::runtime_retry_options(*session_w, runtime_options));
    runtime_transport = &*retry_transport;
    runtime_options.enable_transport_retries = false;
  }
  ava::permissions::register_enforceable_permission_rule_files(session_w->permission_rule_store());

  auto expanded_user_message = runtime_options.expand_prompt_file_references ? expand_prompt_file_references(*session_w, user_message, runtime_options)
                                                                             : ava::core::Result<std::string>(user_message);
  if (!expanded_user_message)
    return fail_run(std::move(expanded_user_message.error()));

  if (auto emitted = ava::event::emit_event(event_sink, session_start_event(*session_w, options.session_mutex)); !emitted)
  {
    return fail_run(std::move(emitted.error()));
  }

  {
    ava::event::MessagePayload user_payload;
    user_payload.text = *expanded_user_message;
    if (auto emitted = ava::event::emit_event(event_sink, ava::event::RuntimeEvent{runtime_event_metadata_2(*session_w, options.session_mutex),
                                                                                   ava::event::UserMessageEvent{.payload = std::move(user_payload)}});
        !emitted)
    {
      return fail_run(std::move(emitted.error()));
    }
  }

  auto const resource_policy = make_extension_resource_policy_1(*session_w);
  bool const include_ambient = !runtime_options.isolate_ambient_extensions;
  bool const include_project_resources = include_ambient && resource_policy.include_project_resources;

  std::shared_ptr<ava::lsp::DiagnosticsProvider> configured_lsp_provider;
  std::vector<ava::agent::SubagentDefinition> subagents;
  if (include_ambient)
  {
    auto lsp_provider = ava::lsp::make_configured_lsp_provider(ava::lsp::ConfiguredLspProviderFiles{
        .global_config_file = resource_policy.global_lsp_config_file,
        .project_config_file = resource_policy.project_lsp_config_file,
        .workspace_root = session_w->workspace_dir(),
        .anchor_set = session_w->anchor_set(),
        .mode = session_w->mode(),
        .permission_resolver = runtime_options.permission_resolver,
    });
    configured_lsp_provider = lsp_provider ? *lsp_provider : nullptr;
    subagents = ava::agent::load_subagents(ava::agent::SubagentLoadOptions{.workspace_root = session_w->workspace_dir(),
                                                                           .include_project_agents = resource_policy.include_project_resources})
                    .subagents;
  }

  auto effective_session_mcp_config = session_w->mcp_config();
  if (runtime_options.disable_session_mcp ||
      (!effective_session_mcp_config && (runtime_options.isolate_ambient_extensions || runtime_options.exact_builtin_tool_names.has_value())))
  {
    effective_session_mcp_config = std::make_shared<ava::mcp::McpConfig const>();
  }

  std::optional<ava::core::Error> sink_error;
  ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{
      .workspace_dir = session_w->workspace_dir(),
      .current_dir = session_w->current_dir(),
      .additional_writable_dirs = session_w->additional_writable_dirs(),
      .anchor_set = session_w->anchor_set(),
      .mode = session_w->mode(),
      .model =
          ava::agent::ModelInvocationOptions{
              .provider_id = session_w->model().provider_id,
              .model_id = session_w->model().model_id,
              .system_prompt = runtime_options.isolate_ambient_extensions ? session_w->ambient_extension_free_system_prompt() : session_w->system_prompt(),
              .stream = runtime_options.stream,
              .supports_tools = session_w->model().supports_tools.value_or(true),
              .supports_streaming = session_w->model().supports_streaming.value_or(true),
              .input_modalities = session_w->model().input_modalities,
              .max_output_tokens = session_w->model().max_output_tokens,
              .reasoning = session_w->reasoning() ? std::optional(runtime::provider_reasoning_options(*session_w->reasoning())) : std::nullopt,
              .pricing = session_w->model().pricing,
              .api_family = session_w->model().api_family,
              .reasoning_format = session_w->model().reasoning_format,
          },
      .access_token = options.access_token,
      .credential_type = options.openai_oauth && options.credential_type == "bearer" ? "oauth" : options.credential_type,
      .openai_oauth = options.openai_oauth,
      .openai_account_id = options.openai_account_id,
      .tool_resources =
          ava::agent::ToolResourceOptions{
              .include_project_resources = include_project_resources,
              .lsp_diagnostics_provider = configured_lsp_provider,
              .plugin_global_plugins_dir = resource_policy.plugin_discovery.global_plugins_dir,
              .plugin_project_plugins_dir = resource_policy.plugin_discovery.project_plugins_dir,
              .plugin_enablement_file = resource_policy.plugin_enablement_file,
              .include_plugin_tools = include_ambient,
              .mcp_global_config_file = resource_policy.mcp_config.global_config_file,
              .mcp_project_config_file = resource_policy.mcp_config.project_config_file,
              .include_global_mcp_config = include_ambient,
              .session_mcp_config = std::move(effective_session_mcp_config),
              .skill_global_dirs = resource_policy.global_skill_dirs,
              .skill_project_dirs = resource_policy.project_skill_dirs,
              .include_global_skills = include_ambient,
              .exact_builtin_tool_names = runtime_options.exact_builtin_tool_names,
          },
      .tool_execution =
          ava::agent::ToolExecutionOptions{
              .require_descriptor_secure_workspace = runtime_options.require_descriptor_secure_workspace,
              .announce_execution_after_permission = runtime_options.announce_execution_after_permission,
              .redact_permission_audit_arguments = runtime_options.redact_permission_audit_arguments,
              .require_explicit_file_permissions = runtime_options.require_explicit_file_permissions,
              .ava_authority_roots = session_w->ava_authority_roots_1(),
              .exact_file_access = runtime_options.exact_file_access,
              .command_executor = runtime_options.command_executor,
          },
      .subagents = std::move(subagents),
      .tool_visibility = session_w->tool_visibility(),
      .on_tool_event =
          [&session_w, &options, &event_sink, &sink_error](ava::agent::ToolTimelineEntry const& entry) {
            if (sink_error)
              return;
            // Capture metadata before mapping payload fields, matching the old base-event path.
            auto metadata = runtime_event_metadata_2(*session_w, options.session_mutex);
            if (auto emitted = ava::event::emit_event(event_sink, runtime_event_from_tool_timeline_entry(std::move(metadata), entry)); !emitted)
            {
              sink_error = std::move(emitted.error());
            }
          },
      .on_tool_progress = [&session_w, &options, &event_sink, &sink_error](ava::agent::ToolProgressEntry const& entry) -> ava::core::VoidResult {
        if (sink_error)
          return std::unexpected(*sink_error);
        auto metadata = runtime_event_metadata_2(*session_w, options.session_mutex);
        if (auto emitted = ava::event::emit_event(event_sink, runtime_event_from_tool_progress_entry(std::move(metadata), entry)); !emitted)
        {
          sink_error = std::move(emitted.error());
          return std::unexpected(*sink_error);
        }
        return {};
      },
      .on_stream_event = [&session_w, &options, &event_sink, &sink_error](ava::provider::StreamEvent const& stream_event) -> ava::core::VoidResult {
        if (sink_error)
          return std::unexpected(*sink_error);
        auto metadata = runtime_event_metadata_2(*session_w, options.session_mutex);
        if (auto emitted = ava::event::emit_event(event_sink, runtime_event_from_provider_stream_event(std::move(metadata), stream_event)); !emitted)
        {
          sink_error = std::move(emitted.error());
          return std::unexpected(*sink_error);
        }
        return {};
      },
      .subagent_launch = {.display = subagent_launch_display,
                          .request_id = subagent_launch_correlation_id,
                          .correlation_id = subagent_launch_correlation_id,
                          .sink = runtime_options.on_subagent_launch},
      .permission_resolver = runtime_options.permission_resolver,
      .auto_allow_deny_preflight = ava::permissions::build_persistent_permission_deny_preflight(session_w->permission_rule_store()),
      .question_resolver = runtime_options.question_resolver,
      .cancel_requested = [&runtime_options,
                           &sink_error] { return sink_error.has_value() || (runtime_options.cancel_requested && runtime_options.cancel_requested()); },
      .take_steering_messages = runtime_options.take_steering_messages,
      .compact_context = runtime_options.access_token.empty() ? decltype(ava::agent::AgentLoopOptions{}.compact_context){}
                                                              : [&](ava::session::SessionReadAuthority read_authority, std::string_view trigger,
                                                                    std::vector<std::string> const& replayed_user_messages) -> ava::core::Result<bool> {
        return runtime::compact_runtime_context(*session_w, std::move(read_authority), trigger, provider, *runtime_transport, runtime_options,
                                                replayed_user_messages);
      },
      .background_provider_factory = [provider_id = session_w->model().provider_id]() -> ava::core::Result<std::unique_ptr<ava::provider::Provider>> {
        return ava::provider::builtin_provider_registry().create(provider_id);
      },
      .background_transport_factory = []() -> ava::core::Result<std::unique_ptr<ava::http::Transport>> {
        std::unique_ptr<ava::http::Transport> transport = std::make_unique<ava::http::CurlCliTransport>();
        return transport;
      },
      .subagent_coordinator = session_w->subagent_coordinator(),
      .session_mutex = runtime_options.session_mutex,
      .append_entry = append_route,
      .append_batch = std::move(append_batch_route),
      .session_read_authority = std::move(*session_read_authority),
      .session_read_limits = session_w->session_read_limits(),
      .synthetic_user_message_provenance = runtime_options.synthetic_subagent_delivery ? runtime_options.synthetic_user_message_provenance : std::nullopt,
      .on_phase = [&guard, &runtime_options](ava::agent::RunPhase phase) -> ava::core::VoidResult {
        if (phase == ava::agent::RunPhase::Completing && runtime_options.on_terminal_commit)
        {
          if (auto committed = runtime_options.on_terminal_commit(); !committed)
            return committed;
        }
        if (auto transitioned = guard.transition(phase); !transitioned)
          return transitioned;
        if (runtime_options.on_phase)
          return runtime_options.on_phase(phase);
        return {};
      },
      .observation = runtime_options.observation,
      .trace_context = runtime_options.trace_context});

  auto result = loop.run_turn(*expanded_user_message, runtime_options.image_attachments, session_w->store, provider, *runtime_transport);
  if (sink_error)
  {
    return fail_run(std::move(*sink_error));
  }
  if (!result)
  {
    auto metadata = runtime_event_metadata_2(*session_w, options.session_mutex);
    if (is_agent_loop_canceled_error(result.error()))
    {
      ava::event::CancellationPayload cancel_payload;
      cancel_payload.text = "stopped by user";
      cancel_payload.error_category = ava::core::to_string(result.error().category());
      cancel_payload.error_message = result.error().message();
      cancel_payload.error_details = result.error().format();
      cancel_payload.reason = result.error().message();
      static_cast<void>(ava::event::emit_event(
          event_sink, ava::event::RuntimeEvent{std::move(metadata), ava::event::CancellationEvent{.payload = std::move(cancel_payload)}}));
    }
    else
    {
      ava::event::ErrorPayload error_payload;
      error_payload.error_category = ava::core::to_string(result.error().category());
      error_payload.error_message = result.error().message();
      error_payload.error_details = result.error().format();
      static_cast<void>(
          ava::event::emit_event(event_sink, ava::event::RuntimeEvent{std::move(metadata), ava::event::ErrorEvent{.payload = std::move(error_payload)}}));
    }
    return fail_run(result.error());
  }

  {
    ava::event::MessagePayload assistant_payload;
    assistant_payload.text = result->final_text;
    if (auto emitted = ava::event::emit_event(event_sink, ava::event::RuntimeEvent{runtime_event_metadata_2(*session_w, options.session_mutex),
                                                                                   ava::event::AssistantMessageEvent{.payload = std::move(assistant_payload)}});
        !emitted)
    {
      return fail_run(std::move(emitted.error()));
    }
  }

  {
    ava::event::CompletionPayload completion_payload;
    completion_payload.stop_reason = std::string(ava::core::to_string(result->outcome));
    completion_payload.provider_iterations = result->provider_iterations;
    completion_payload.tool_calls = result->tool_calls;
    if (auto emitted = ava::event::emit_event(event_sink, ava::event::RuntimeEvent{runtime_event_metadata_2(*session_w, options.session_mutex),
                                                                                   ava::event::CompletionEvent{.payload = std::move(completion_payload)}});
        !emitted)
    {
      return fail_run(std::move(emitted.error()));
    }
  }
  if (auto transitioned = guard.transition(RunPhase::Completing); !transitioned)
    return fail_run(std::move(transitioned.error()));
  auto const proposed_reason = stop_reason_for_runtime_outcome(result->outcome);
  auto completed = guard.complete(RunOutcome{.run_id = {}, .reason = proposed_reason});
  if (!completed)
    return std::unexpected(std::move(completed.error()));
  if (completed->reason == StopReason::PersistenceError && completed->error)
  {
    if (session_w->diagnostics())
      session_w->diagnostics()->record_terminal_failure(ava::diagnostics::RuntimeFailureClass::Session, *completed->error);
    return std::unexpected(*completed->error);
  }
  if (completed->reason != proposed_reason)
    result->outcome = runtime_outcome_for_stop_reason(completed->reason);

  // This boundary is deliberately after AdmissionGuard completion, not the
  // earlier Done event. The coordinator is best-effort and cannot change the
  // already committed ordinary user turn.
  if (result->committed_turn_id && !options.synthetic_subagent_delivery && !session_w->sessionless() && session_w->session_title_coordinator())
  {
    session_w->session_title_coordinator()->schedule(*session_w, user_message, *result->committed_turn_id, options);
  }

  return result;
}

}  // namespace ava::app
