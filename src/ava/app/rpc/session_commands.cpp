#include "sys.h"
#include "output.h"
#include "runtime_navigation.h"
#include "serialization.h"
#include "serialization_json.h"
#include "session_commands.h"
#include "session_operators.h"
#include "ava/event/events.h"
#include "ava/app/runtime/Session.h"
#include "ava/agent/job_control.h"
#include "ava/session/session_branch.h"
#include "ava/session/session_metadata.h"

#include <algorithm>
#include <chrono>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

namespace ava::app::rpc {
namespace {

ava::core::Result<bool> handled(ava::core::VoidResult result)
{
  if (!result)
    return std::unexpected(std::move(result.error()));
  return true;
}

std::string_view trim_rpc_text(std::string_view text)
{
  auto const first = text.find_first_not_of(" \t\r\n");
  if (first == std::string_view::npos)
    return {};
  auto const last = text.find_last_not_of(" \t\r\n");
  return text.substr(first, last - first + 1);
}

ava::core::Result<bool> reject_active_run_if_needed(RpcSessionCommandContext const& context)
{
  if (!active_run(context.run_state))
    return false;
  return handled(write_error(context.output, context.command.id, active_run_reject_error(context.command.type)));
}

void reset_cancel_after_session_switch(RpcRunState& run_state)
{
  std::lock_guard state_lock(run_state.mutex);
  run_state.cancel_requested.store(false, std::memory_order_relaxed);
}

ava::core::Result<std::string> resolve_branch_source_session_id(runtime::Session const& current, runtime::OpenContext const& open_context,
                                                                RpcCommand const& command)
{
  if (command.session_id && command.session_id->empty())
    return std::unexpected(invalid_rpc(command.type + " session_id must be non-empty when provided"));
  if (!command.session_id)
    return current.store.session_id();

  auto sessions = ava::session::SessionStore::list_sessions(current.workspace_dir(), current.paths().sessions_dir);
  if (!sessions)
    return std::unexpected(std::move(sessions.error()));
  std::vector<std::string> matches;
  for (auto const& session : *sessions)
  {
    if (session.session_id == *command.session_id || (!open_context.exact_session_id && session.session_id.starts_with(*command.session_id)))
      matches.push_back(session.session_id);
  }
  if (matches.empty())
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::NotFound, "session not found");
    error.with_context("session_id", *command.session_id);
    return std::unexpected(std::move(error));
  }
  if (matches.size() > 1)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "session id prefix is ambiguous");
    error.with_context("session_id", *command.session_id).with_context("matches", std::to_string(matches.size()));
    return std::unexpected(std::move(error));
  }
  return matches.front();
}

ava::core::Result<ava::permissions::PermissionRuleDraft> permission_rule_draft_from_command(RpcCommand const& command)
{
  if (!command.action || command.action->empty())
  {
    return std::unexpected(invalid_rpc("permission_rule_add requires action"));
  }
  auto const action = ava::permissions::parse_permission_action(*command.action);
  if (!action || *action == ava::permissions::PermissionAction::Ask)
  {
    auto error = invalid_rpc("permission_rule_add action must be allow or deny");
    error.with_context("action", *command.action);
    return std::unexpected(std::move(error));
  }

  if (!command.operation || command.operation->empty())
  {
    return std::unexpected(invalid_rpc("permission_rule_add requires operation"));
  }
  auto const operation = ava::permissions::parse_operation(*command.operation);
  if (!operation)
  {
    auto error = invalid_rpc("permission_rule_add operation is unsupported");
    error.with_context("operation", *command.operation);
    return std::unexpected(std::move(error));
  }

  auto scope = ava::permissions::PermissionRuleScope::Workspace;
  if (command.scope && !command.scope->empty())
  {
    auto parsed_scope = ava::permissions::parse_permission_rule_scope(*command.scope);
    if (!parsed_scope)
    {
      auto error = invalid_rpc("permission_rule_add scope must be workspace or global");
      error.with_context("scope", *command.scope);
      return std::unexpected(std::move(error));
    }
    scope = *parsed_scope;
  }

  auto mode = ava::permissions::PermissionRuleMode::Any;
  if (command.mode && !command.mode->empty())
  {
    auto parsed_mode = ava::permissions::parse_permission_rule_mode(*command.mode);
    if (!parsed_mode)
    {
      auto error = invalid_rpc("permission_rule_add mode must be any, build, or plan");
      error.with_context("mode", *command.mode);
      return std::unexpected(std::move(error));
    }
    mode = *parsed_mode;
  }

  if (!command.reason || command.reason->empty())
  {
    return std::unexpected(invalid_rpc("permission_rule_add requires reason"));
  }

  auto target_path = command.target_path ? std::filesystem::path(*command.target_path) : std::filesystem::path{};
  if (target_path.empty() && command.path)
    target_path = *command.path;
  return ava::permissions::PermissionRuleDraft{.scope = scope,
                                               .action = *action,
                                               .operation = *operation,
                                               .mode = mode,
                                               .tool_name = command.tool_name.value_or(""),
                                               .target_path = std::move(target_path),
                                               .command = command.command.value_or(""),
                                               .command_recipe_key = command.command_recipe_key.value_or(""),
                                               .recipe_display = command.recipe_display.value_or(""),
                                               .critical_acknowledged = command.critical_acknowledged.value_or(false),
                                               .reason = *command.reason,
                                               .actor = "rpc"};
}

std::string permission_rule_added_json(ava::permissions::PersistentPermissionRule const& rule)
{
  return std::string("{\"rule\":") + ava::permissions::permission_rule_json(rule) + "}";
}

std::string permission_rule_removed_json(ava::permissions::PersistentPermissionRule const& rule)
{
  return std::string("{\"removed\":true,\"rule\":") + ava::permissions::permission_rule_json(rule) + "}";
}

ava::core::Error safe_job_rpc_error(ava::core::Error const& source)
{
  auto safe = ava::core::Error(source.category(), source.message());
  if (std::ranges::any_of(source.context(), [](ava::core::ErrorContext const& item) { return item.key == "job_error_code" && item.value == "job_not_ready"; }))
    safe.with_context("rpc_error_code", "job_not_ready");
  return safe;
}

std::string branch_summary_result_json(ava::session::BranchSummaryResult const& result)
{
  std::string json = "{";
  json += string_field_json("source_session_id", result.source_session_id);
  json += ',';
  json += string_field_json("entry_id", result.entry.id);
  json += ',';
  json += string_field_json("parent_id", result.entry.parent_id);
  json += ',';
  json += string_field_json("timestamp", result.entry.timestamp);
  json += ",\"entry\":{";
  json += integer_field_json("version", result.entry.version);
  json += ',';
  json += string_field_json("id", result.entry.id);
  json += ',';
  json += string_field_json("parent_id", result.entry.parent_id);
  json += ',';
  json += string_field_json("type", ava::session::to_string(result.entry.type));
  json += ',';
  json += string_field_json("timestamp", result.entry.timestamp);
  json += ",\"data\":";
  json += result.entry.data_json;
  json += "}}";
  return json;
}

}  // namespace

ava::core::Result<bool> handle_session_rpc_command(RpcSessionCommandContext context)
{
  using session_ts = ava::app::runtime::session_ts;

  auto const& command = context.command;
  auto& session = context.unlocked_session;

  if (command.type == "get_state")
  {
    bool const canceled = cancel_requested(context.run_state);

    return handled(write_success(context.output, command.id, session_ts::rat(session)->state_result_json(canceled)));
  }

  if (command.type == "list_sessions")
  {
    auto sessions_json = session_ts::rat(session)->list_sessions_result_json();
    if (!sessions_json)
      return handled(write_error(context.output, command.id, sessions_json.error()));
    return handled(write_success(context.output, command.id, *sessions_json));
  }

  if (command.type == "list_jobs" || command.type == "get_job" || command.type == "wait_job" || command.type == "get_job_result" ||
      command.type == "cancel_job" || command.type == "promote_job")
  {
    std::shared_ptr<ava::agent::SubagentCoordinator> coordinator;
    std::string owner;
    {
      session_ts::rat session_r(session);
      coordinator = session_r->subagent_coordinator();
      owner = session_r->store.session_id();
    }
    if (!coordinator)
      return handled(write_error(context.output, command.id, invalid_rpc("job controls are unavailable")));
    if (command.type == "list_jobs")
    {
      if (command.job_id || command.timeout_ms)
        return handled(write_error(context.output, command.id, invalid_rpc("list_jobs accepts no job payload fields")));
      return handled(write_success(context.output, command.id, ava::agent::public_job_list_json(coordinator->list(owner))));
    }
    if (!command.job_id)
      return handled(write_error(context.output, command.id, invalid_rpc(command.type + " requires job_id")));

    ava::core::Result<ava::agent::SubagentCoordinatorJobSnapshot> snapshot =
        std::unexpected(ava::core::Error(ava::core::ErrorCategory::Tool, "job RPC action was not dispatched"));
    auto content = ava::agent::PublicJobContent::OmitTerminalContent;
    if (command.type == "get_job")
      snapshot = coordinator->snapshot(owner, *command.job_id);
    else if (command.type == "wait_job")
    {
      auto const timeout_ms = std::min(command.timeout_ms.value_or(ava::agent::kDefaultPublicJobWaitTimeoutMs), ava::agent::kMaxPublicJobWaitTimeoutMs);
      snapshot = coordinator->wait(owner, *command.job_id, std::chrono::milliseconds(timeout_ms));
    }
    else if (command.type == "get_job_result")
    {
      snapshot = coordinator->result(owner, *command.job_id);
      content = ava::agent::PublicJobContent::IncludeTerminalResult;
    }
    else if (command.type == "cancel_job")
      snapshot = coordinator->cancel(owner, *command.job_id);
    else if (command.type == "promote_job")
      snapshot = coordinator->promote(owner, *command.job_id);
    if (!snapshot)
      return handled(write_error(context.output, command.id, safe_job_rpc_error(snapshot.error())));
    return handled(write_success(context.output, command.id, ava::agent::public_job_snapshot_json(*snapshot, content)));
  }

  if (command.type == "session_tree")
  {
    auto tree_json = session_ts::rat(session)->tree_result_json();
    if (!tree_json)
      return handled(write_error(context.output, command.id, tree_json.error()));
    return handled(write_success(context.output, command.id, *tree_json));
  }

  if (command.type == "list_models")
  {
    auto models_json = session_ts::rat(session)->list_models_result_json();
    if (!models_json)
      return handled(write_error(context.output, command.id, models_json.error()));
    return handled(write_success(context.output, command.id, *models_json));
  }

  if (command.type == "get_messages")
  {
    auto active_rejected = reject_active_run_if_needed(context);
    if (!active_rejected || *active_rejected)
      return active_rejected;

    auto messages_json = session_ts::rat(session)->messages_result_json();
    if (!messages_json)
      return handled(write_error(context.output, command.id, messages_json.error()));
    return handled(write_success(context.output, command.id, *messages_json));
  }

  if (command.type == "get_session_stats")
  {
    auto active_rejected = reject_active_run_if_needed(context);
    if (!active_rejected || *active_rejected)
      return active_rejected;

    auto stats_json = session_ts::rat(session)->stats_result_json();
    if (!stats_json)
      return handled(write_error(context.output, command.id, stats_json.error()));
    return handled(write_success(context.output, command.id, *stats_json));
  }

  if (command.type == "validate_session")
  {
    auto active_rejected = reject_active_run_if_needed(context);
    if (!active_rejected || *active_rejected)
      return active_rejected;

    auto validation_json = session_ts::rat(session)->validation_result_json();
    if (!validation_json)
      return handled(write_error(context.output, command.id, validation_json.error()));
    return handled(write_success(context.output, command.id, *validation_json));
  }

  if (command.type == "session_metadata")
  {
    auto metadata = session_ts::rat(session)->load_metadata();
    if (!metadata)
      return handled(write_error(context.output, command.id, metadata.error()));
    return handled(write_success(context.output, command.id, ava::session::session_metadata_json(*metadata)));
  }

  if (bool const command_is_set_session_name = command.type == "set_session_name", command_is_set_session_labels = command.type == "set_session_labels";
      command_is_set_session_name || command_is_set_session_labels)
  {
    auto active_rejected = reject_active_run_if_needed(context);
    if (!active_rejected || *active_rejected)
      return active_rejected;
    ava::session::SessionMetadataUpdate update;

    if (command_is_set_session_name)
    {
      if (!command.session_name)
        return handled(write_error(context.output, command.id, invalid_rpc("set_session_name requires session_name")));
      update.name = *command.session_name;
    }
    else
    {
      if (!command.labels)
        return handled(write_error(context.output, command.id, invalid_rpc("set_session_labels requires labels")));
      update.labels = *command.labels;
    }

    update.actor = "rpc";
    auto metadata = session_ts::wat(session)->append_metadata(std::move(update));
    if (!metadata)
      return handled(write_error(context.output, command.id, metadata.error()));
    return handled(write_success(context.output, command.id, ava::session::session_metadata_json(*metadata)));
  }

  if (command.type == "permission_rules")
  {
    auto active_rejected = reject_active_run_if_needed(context);
    if (!active_rejected || *active_rejected)
      return active_rejected;

    auto store = session_ts::rat(session)->permission_rule_store();
    auto rules = ava::permissions::load_persistent_permission_rules(store);
    if (!rules)
      return handled(write_error(context.output, command.id, rules.error()));
    return handled(write_success(context.output, command.id, ava::permissions::permission_rules_result_json(store, *rules)));
  }

  if (command.type == "permission_rule_add")
  {
    auto active_rejected = reject_active_run_if_needed(context);
    if (!active_rejected || *active_rejected)
      return active_rejected;

    auto draft = permission_rule_draft_from_command(command);
    if (!draft)
      return handled(write_error(context.output, command.id, draft.error()));
    ava::permissions::PermissionRuleStore store;
    std::string session_id;
    {
      session_ts::rat session_r(session);
      store = session_r->permission_rule_store();
      session_id = session_r->store.session_id();
    }
    auto added = ava::permissions::add_persistent_permission_rule(store, std::move(*draft));
    if (!added)
      return handled(write_error(context.output, command.id, added.error()));
    auto const json = permission_rule_added_json(*added);
    auto envelope = resolver_event_envelope("permission_rule_added", command.id, command.id, session_id, json);
    if (auto written = Output::write_record(context.output, ava::event::serialize_event_envelope_jsonl(envelope)); !written)
    {
      return std::unexpected(std::move(written.error()));
    }
    return handled(write_success(context.output, command.id, json));
  }

  if (command.type == "permission_rule_remove")
  {
    auto active_rejected = reject_active_run_if_needed(context);
    if (!active_rejected || *active_rejected)
      return active_rejected;

    if (!command.rule_id || command.rule_id->empty())
    {
      return handled(write_error(context.output, command.id, invalid_rpc("permission_rule_remove requires rule_id")));
    }
    ava::permissions::PermissionRuleStore store;
    std::string session_id;
    {
      session_ts::rat session_r(session);
      store = session_r->permission_rule_store();
      session_id = session_r->store.session_id();
    }
    auto removed = ava::permissions::remove_persistent_permission_rule(store, *command.rule_id);
    if (!removed)
      return handled(write_error(context.output, command.id, removed.error()));
    auto const json = permission_rule_removed_json(*removed);
    auto envelope = resolver_event_envelope("permission_rule_removed", command.id, command.id, session_id, json);
    if (auto written = Output::write_record(context.output, ava::event::serialize_event_envelope_jsonl(envelope)); !written)
    {
      return std::unexpected(std::move(written.error()));
    }
    return handled(write_success(context.output, command.id, json));
  }

  if (command.type == "set_model" || command.type == "cycle_model")
  {
    auto active_rejected = reject_active_run_if_needed(context);
    if (!active_rejected || *active_rejected)
      return active_rejected;

    session_ts::wat session_w(session);
    ava::core::Result<ava::config::ModelInfo> selected =
        command.type == "set_model" ? resolve_requested_model(session_w, command) : next_runtime_model(session_w);
    if (!selected)
      return handled(write_error(context.output, command.id, selected.error()));

    auto switched = session_w->switch_model(std::move(*selected));
    if (!switched)
      return handled(write_error(context.output, command.id, switched.error()));
    return handled(write_success(context.output, command.id, session_w->state_result_json(cancel_requested(context.run_state))));
  }

  if (command.type == "set_reasoning" || command.type == "clear_reasoning")
  {
    auto active_rejected = reject_active_run_if_needed(context);
    if (!active_rejected || *active_rejected)
      return active_rejected;

    std::optional<runtime::ReasoningSelection> selection = std::nullopt;
    if (command.type == "set_reasoning")
    {
      if (!command.reasoning_level || command.reasoning_level->empty())
      {
        return handled(write_error(context.output, command.id, invalid_rpc("set_reasoning requires reasoning_level")));
      }
      auto const level = trim_rpc_text(*command.reasoning_level);
      if (level.empty())
      {
        return handled(write_error(context.output, command.id, invalid_rpc("set_reasoning requires reasoning_level")));
      }
      if (level != "off")
      {
        selection = runtime::ReasoningSelection{
            .level = std::string(level), .budget_tokens = command.reasoning_budget_tokens, .display = command.reasoning_display.value_or("")};
      }
    }

    session_ts::wat session_w(session);
    auto changed = session_w->set_reasoning(std::move(selection));
    if (!changed)
      return handled(write_error(context.output, command.id, changed.error()));
    return handled(write_success(context.output, command.id, session_w->state_result_json(cancel_requested(context.run_state))));
  }

  if (command.type == "fork_session" || command.type == "clone_session")
  {
    auto active_rejected = reject_active_run_if_needed(context);
    if (!active_rejected || *active_rejected)
      return active_rejected;
    if (command.type == "clone_session" && command.branch_from_entry_id)
    {
      return handled(write_error(context.output, command.id, invalid_rpc("clone_session does not support branch_from_entry_id")));
    }

    session_ts::wat session_w(session);
    // FIXME: resolve_branch_source_session_id should accept a rat.
    auto source_session_id = resolve_branch_source_session_id(*session_w, context.open_context, command);
    if (!source_session_id)
      return handled(write_error(context.output, command.id, source_session_id.error()));

    std::optional<ava::session::SessionLease> temporary_source_lease;
    if (auto recovered = session_w->recover_source_for_mutation(*source_session_id, temporary_source_lease); !recovered)
      return handled(write_error(context.output, command.id, recovered.error()));

    auto const* source_lease = *source_session_id == session_w->store.session_id() ? &session_w->lease() : &*temporary_source_lease;
    auto branched = ava::session::create_session_branch(ava::session::SessionBranchOptions{
        .workspace_dir = session_w->workspace_dir(),
        .root_dir = session_w->paths().sessions_dir,
        .source_session_id = *source_session_id,
        .branch_from_entry_id = command.branch_from_entry_id.value_or(""),
        .name = command.session_name,
        .labels = command.labels,
        .read_limits = session_w->session_read_limits(),
        .source_lease = source_lease,
        .mode = command.type == "clone_session" ? ava::session::SessionBranchMode::Clone : ava::session::SessionBranchMode::Fork,
        .actor = "rpc"});
    if (!branched)
      return handled(write_error(context.output, command.id, branched.error()));

    auto owned_options = session_w->replacement_open_context(context.open_context);
    auto unlocked_opened_result = ava::app::runtime::Session::open_owned(owned_options, branched->store, branched->lease, true);
    if (!unlocked_opened_result)
    {
      auto error = std::move(unlocked_opened_result.error());
      ava::session::rollback_created_session_with_context(branched->store, branched->lease, error);
      return handled(write_error(context.output, command.id, error));
    }
    {
      runtime::session_ts::wat opened_w(*unlocked_opened_result);
      opened_w->created = true;
      if (auto replaced = session_w->replace_with(std::move(*opened_w)); !replaced)
        return handled(write_error(context.output, command.id, replaced.error()));
    }
    reset_cancel_after_session_switch(context.run_state);
    return handled(write_success(context.output, command.id, session_w->state_result_json(false)));
  }

  if (command.type == "summarize_branch")
  {
    auto active_rejected = reject_active_run_if_needed(context);
    if (!active_rejected || *active_rejected)
      return active_rejected;
    if (!command.branch_root_entry_id || command.branch_root_entry_id->empty())
    {
      return handled(write_error(context.output, command.id, invalid_rpc("summarize_branch requires branch_root_entry_id")));
    }
    if (!command.branch_tip_entry_id || command.branch_tip_entry_id->empty())
    {
      return handled(write_error(context.output, command.id, invalid_rpc("summarize_branch requires branch_tip_entry_id")));
    }
    if (!command.summary || command.summary->empty())
    {
      return handled(write_error(context.output, command.id, invalid_rpc("summarize_branch requires summary")));
    }
    if (!command.provider || command.provider->empty())
    {
      return handled(write_error(context.output, command.id, invalid_rpc("summarize_branch requires provider")));
    }
    if (!command.model || command.model->empty())
    {
      return handled(write_error(context.output, command.id, invalid_rpc("summarize_branch requires model")));
    }
    if (!command.reason || command.reason->empty())
    {
      return handled(write_error(context.output, command.id, invalid_rpc("summarize_branch requires reason")));
    }

    session_ts::wat session_w(session);
    // FIXME: resolve_branch_source_session_id should accept a rat.
    auto source_session_id = resolve_branch_source_session_id(*session_w, context.open_context, command);
    if (!source_session_id)
      return handled(write_error(context.output, command.id, source_session_id.error()));
    bool const current_source = *source_session_id == session_w->store.session_id();
    std::optional<ava::session::SessionLease> temporary_source_lease;
    if (auto recovered = session_w->recover_source_for_mutation(*source_session_id, temporary_source_lease); !recovered)
      return handled(write_error(context.output, command.id, recovered.error()));
    auto const* source_lease = current_source ? &session_w->lease() : &*temporary_source_lease;
    auto options = ava::session::BranchSummaryOptions{.workspace_dir = session_w->workspace_dir(),
                                                      .root_dir = session_w->paths().sessions_dir,
                                                      .source_session_id = *source_session_id,
                                                      .branch_root_entry_id = *command.branch_root_entry_id,
                                                      .branch_tip_entry_id = *command.branch_tip_entry_id,
                                                      .summary = *command.summary,
                                                      .provider = *command.provider,
                                                      .model = *command.model,
                                                      .reason = *command.reason,
                                                      .source_lease = source_lease,
                                                      .actor = "rpc"};

    if (current_source)
    {
      auto prepared = ava::session::prepare_branch_summary(std::move(options));
      if (!prepared)
        return handled(write_error(context.output, command.id, prepared.error()));
      auto owner_append = session_w->owner_append_route();
      session_w.unlock();
      if (!owner_append)
        return handled(write_error(context.output, command.id,
                                   ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "runtime session owner append route is unavailable")));
      if (auto appended = owner_append(prepared->entry); !appended)
        return handled(write_error(context.output, command.id, appended.error()));
      return handled(write_success(context.output, command.id, branch_summary_result_json(*prepared)));
    }

    // The temporary lease remains local and active after releasing the runtime
    // session mutex; noncurrent summaries intentionally append directly through it.
    session_w.unlock();
    auto summary = ava::session::append_branch_summary(std::move(options));
    if (!summary)
      return handled(write_error(context.output, command.id, summary.error()));
    return handled(write_success(context.output, command.id, branch_summary_result_json(*summary)));
  }

  if (command.type == "new_session")
  {
    auto active_rejected = reject_active_run_if_needed(context);
    if (!active_rejected || *active_rejected)
      return active_rejected;

    session_ts::wat session_w(session);

    auto unlocked_created_result = session_w->create_similar(context.open_context);
    if (!unlocked_created_result)
      return handled(write_error(context.output, command.id, unlocked_created_result.error()));
    {
      session_ts::wat created_w(*unlocked_created_result);
      if (auto replaced = session_w->replace_with(std::move(*created_w)); !replaced)
        return handled(write_error(context.output, command.id, replaced.error()));
    }
    reset_cancel_after_session_switch(context.run_state);
    return handled(write_success(context.output, command.id, session_w->state_result_json(false)));
  }

  if (command.type == "open_session" || command.type == "switch_session")
  {
    if (!command.session_id || command.session_id->empty())
    {
      return handled(write_error(context.output, command.id, invalid_rpc(command.type + " requires session_id")));
    }
    auto active_rejected = reject_active_run_if_needed(context);
    if (!active_rejected || *active_rejected)
      return active_rejected;

    session_ts::wat session_w(session);
    auto unlocked_opened_result = session_w->open_requested(context.open_context, *command.session_id);
    if (!unlocked_opened_result)
      return handled(write_error(context.output, command.id, unlocked_opened_result.error()));
    {
      session_ts::wat opened_w(*unlocked_opened_result);
      if (auto replaced = session_w->replace_with(std::move(*opened_w)); !replaced)
        return handled(write_error(context.output, command.id, replaced.error()));
    }
    reset_cancel_after_session_switch(context.run_state);
    return handled(write_success(context.output, command.id, session_w->state_result_json(false)));
  }

  return false;
}

}  // namespace ava::app::rpc
