#include "sys.h"
#include "output.h"
#include "serialization.h"
#include "serialization_json.h"
#include "session_commands.h"
#include "session_operators.h"
#include "ava/session/session_branch.h"
#include "ava/session/session_metadata.h"

#include <optional>
#include <utility>

namespace ava::app::rpc {
namespace {

ava::core::Result<bool> handled(ava::core::VoidResult result)
{
  if (!result)
    return std::unexpected(std::move(result.error()));
  return true;
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

ava::core::Result<std::string> resolve_branch_source_session_id(RuntimeSession const& current, RuntimeOpenOptions const& open_options,
                                                                RpcCommand const& command)
{
  if (command.session_id && command.session_id->empty())
  {
    return std::unexpected(invalid_rpc(command.type + " session_id must be non-empty when provided"));
  }
  if (!command.session_id)
    return current.store.session_id();
  auto source = open_requested_session(current, open_options, *command.session_id);
  if (!source)
    return std::unexpected(std::move(source.error()));
  return source->store.session_id();
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
  auto const& command = context.command;

  if (command.type == "get_state")
  {
    bool const canceled = cancel_requested(context.run_state);
    std::lock_guard lock(context.session_mutex);
    return handled(write_success(context.output, command.id, state_result_json(context.session, canceled)));
  }

  if (command.type == "list_sessions")
  {
    std::lock_guard lock(context.session_mutex);
    auto sessions_json = list_sessions_result_json(context.session);
    if (!sessions_json)
      return handled(write_error(context.output, command.id, sessions_json.error()));
    return handled(write_success(context.output, command.id, *sessions_json));
  }

  if (command.type == "session_tree")
  {
    std::lock_guard lock(context.session_mutex);
    auto tree_json = session_tree_result_json(context.session);
    if (!tree_json)
      return handled(write_error(context.output, command.id, tree_json.error()));
    return handled(write_success(context.output, command.id, *tree_json));
  }

  if (command.type == "list_models")
  {
    std::lock_guard lock(context.session_mutex);
    auto models_json = list_models_result_json(context.session);
    if (!models_json)
      return handled(write_error(context.output, command.id, models_json.error()));
    return handled(write_success(context.output, command.id, *models_json));
  }

  if (command.type == "get_messages")
  {
    auto active_rejected = reject_active_run_if_needed(context);
    if (!active_rejected || *active_rejected)
      return active_rejected;

    std::lock_guard lock(context.session_mutex);
    auto messages_json = messages_result_json(context.session);
    if (!messages_json)
      return handled(write_error(context.output, command.id, messages_json.error()));
    return handled(write_success(context.output, command.id, *messages_json));
  }

  if (command.type == "get_session_stats")
  {
    auto active_rejected = reject_active_run_if_needed(context);
    if (!active_rejected || *active_rejected)
      return active_rejected;

    std::lock_guard lock(context.session_mutex);
    auto stats_json = session_stats_result_json(context.session);
    if (!stats_json)
      return handled(write_error(context.output, command.id, stats_json.error()));
    return handled(write_success(context.output, command.id, *stats_json));
  }

  if (command.type == "validate_session")
  {
    auto active_rejected = reject_active_run_if_needed(context);
    if (!active_rejected || *active_rejected)
      return active_rejected;

    std::lock_guard lock(context.session_mutex);
    auto validation_json = session_validation_result_json(context.session);
    if (!validation_json)
      return handled(write_error(context.output, command.id, validation_json.error()));
    return handled(write_success(context.output, command.id, *validation_json));
  }

  if (command.type == "session_metadata")
  {
    std::lock_guard lock(context.session_mutex);
    auto metadata = ava::session::load_session_metadata(context.session.store);
    if (!metadata)
      return handled(write_error(context.output, command.id, metadata.error()));
    return handled(write_success(context.output, command.id, ava::session::session_metadata_json(context.session.store.session_id(), *metadata)));
  }

  if (command.type == "set_session_name")
  {
    auto active_rejected = reject_active_run_if_needed(context);
    if (!active_rejected || *active_rejected)
      return active_rejected;
    if (!command.session_name)
    {
      return handled(write_error(context.output, command.id, invalid_rpc("set_session_name requires session_name")));
    }

    std::lock_guard lock(context.session_mutex);
    ava::session::SessionMetadataUpdate update;
    update.name = *command.session_name;
    update.actor = "rpc";
    auto metadata = ava::session::append_session_metadata(context.session.store, std::move(update));
    if (!metadata)
      return handled(write_error(context.output, command.id, metadata.error()));
    return handled(write_success(context.output, command.id, ava::session::session_metadata_json(context.session.store.session_id(), *metadata)));
  }

  if (command.type == "set_session_labels")
  {
    auto active_rejected = reject_active_run_if_needed(context);
    if (!active_rejected || *active_rejected)
      return active_rejected;
    if (!command.labels)
    {
      return handled(write_error(context.output, command.id, invalid_rpc("set_session_labels requires labels")));
    }

    std::lock_guard lock(context.session_mutex);
    ava::session::SessionMetadataUpdate update;
    update.labels = *command.labels;
    update.actor = "rpc";
    auto metadata = ava::session::append_session_metadata(context.session.store, std::move(update));
    if (!metadata)
      return handled(write_error(context.output, command.id, metadata.error()));
    return handled(write_success(context.output, command.id, ava::session::session_metadata_json(context.session.store.session_id(), *metadata)));
  }

  if (command.type == "permission_rules")
  {
    auto active_rejected = reject_active_run_if_needed(context);
    if (!active_rejected || *active_rejected)
      return active_rejected;

    ava::permissions::PermissionRuleStore store;
    {
      std::lock_guard lock(context.session_mutex);
      store = permission_rule_store_for_session(context.session);
    }
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
      std::lock_guard lock(context.session_mutex);
      store = permission_rule_store_for_session(context.session);
      session_id = context.session.store.session_id();
    }
    auto added = ava::permissions::add_persistent_permission_rule(store, std::move(*draft));
    if (!added)
      return handled(write_error(context.output, command.id, added.error()));
    auto const json = permission_rule_added_json(*added);
    auto envelope = resolver_event_envelope("permission_rule_added", command.id, command.id, session_id, json);
    if (auto written = write_record(context.output, serialize_event_envelope_jsonl(envelope)); !written)
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
      std::lock_guard lock(context.session_mutex);
      store = permission_rule_store_for_session(context.session);
      session_id = context.session.store.session_id();
    }
    auto removed = ava::permissions::remove_persistent_permission_rule(store, *command.rule_id);
    if (!removed)
      return handled(write_error(context.output, command.id, removed.error()));
    auto const json = permission_rule_removed_json(*removed);
    auto envelope = resolver_event_envelope("permission_rule_removed", command.id, command.id, session_id, json);
    if (auto written = write_record(context.output, serialize_event_envelope_jsonl(envelope)); !written)
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

    std::lock_guard lock(context.session_mutex);
    ava::core::Result<ava::config::ModelInfo> selected =
        command.type == "set_model" ? resolve_requested_model(context.session, command) : next_runtime_model(context.session);
    if (!selected)
      return handled(write_error(context.output, command.id, selected.error()));

    auto switched = switch_runtime_model(context.session, std::move(*selected));
    if (!switched)
      return handled(write_error(context.output, command.id, switched.error()));
    return handled(write_success(context.output, command.id, state_result_json(context.session, cancel_requested(context.run_state))));
  }

  if (command.type == "set_reasoning" || command.type == "clear_reasoning")
  {
    auto active_rejected = reject_active_run_if_needed(context);
    if (!active_rejected || *active_rejected)
      return active_rejected;

    std::optional<RuntimeReasoningSelection> selection = std::nullopt;
    if (command.type == "set_reasoning")
    {
      if (!command.reasoning_level || command.reasoning_level->empty())
      {
        return handled(write_error(context.output, command.id, invalid_rpc("set_reasoning requires reasoning_level")));
      }
      selection = RuntimeReasoningSelection{
          .level = *command.reasoning_level, .budget_tokens = command.reasoning_budget_tokens, .display = command.reasoning_display.value_or("")};
    }

    std::lock_guard lock(context.session_mutex);
    auto changed = set_runtime_reasoning(context.session, std::move(selection));
    if (!changed)
      return handled(write_error(context.output, command.id, changed.error()));
    return handled(write_success(context.output, command.id, state_result_json(context.session, cancel_requested(context.run_state))));
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

    std::lock_guard lock(context.session_mutex);
    auto source_session_id = resolve_branch_source_session_id(context.session, context.open_options, command);
    if (!source_session_id)
      return handled(write_error(context.output, command.id, source_session_id.error()));

    auto branched = ava::session::create_session_branch(ava::session::SessionBranchOptions{
        .workspace_dir = context.session.workspace_dir,
        .root_dir = context.session.paths.sessions_dir,
        .source_session_id = *source_session_id,
        .branch_from_entry_id = command.branch_from_entry_id.value_or(""),
        .name = command.session_name,
        .labels = command.labels,
        .mode = command.type == "clone_session" ? ava::session::SessionBranchMode::Clone : ava::session::SessionBranchMode::Fork,
        .actor = "rpc"});
    if (!branched)
      return handled(write_error(context.output, command.id, branched.error()));

    auto opened = open_requested_session(context.session, context.open_options, branched->store.session_id());
    if (!opened)
      return handled(write_error(context.output, command.id, opened.error()));
    opened->created = true;
    context.session = std::move(*opened);
    reset_cancel_after_session_switch(context.run_state);
    return handled(write_success(context.output, command.id, state_result_json(context.session, false)));
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

    std::lock_guard lock(context.session_mutex);
    auto source_session_id = resolve_branch_source_session_id(context.session, context.open_options, command);
    if (!source_session_id)
      return handled(write_error(context.output, command.id, source_session_id.error()));
    auto summary = ava::session::append_branch_summary(ava::session::BranchSummaryOptions{.workspace_dir = context.session.workspace_dir,
                                                                                          .root_dir = context.session.paths.sessions_dir,
                                                                                          .source_session_id = *source_session_id,
                                                                                          .branch_root_entry_id = *command.branch_root_entry_id,
                                                                                          .branch_tip_entry_id = *command.branch_tip_entry_id,
                                                                                          .summary = *command.summary,
                                                                                          .provider = *command.provider,
                                                                                          .model = *command.model,
                                                                                          .reason = *command.reason,
                                                                                          .actor = "rpc"});
    if (!summary)
      return handled(write_error(context.output, command.id, summary.error()));
    return handled(write_success(context.output, command.id, branch_summary_result_json(*summary)));
  }

  if (command.type == "new_session")
  {
    auto active_rejected = reject_active_run_if_needed(context);
    if (!active_rejected || *active_rejected)
      return active_rejected;

    std::lock_guard lock(context.session_mutex);
    auto created = create_new_session(context.session, context.open_options);
    if (!created)
      return handled(write_error(context.output, command.id, created.error()));
    context.session = std::move(*created);
    reset_cancel_after_session_switch(context.run_state);
    return handled(write_success(context.output, command.id, state_result_json(context.session, false)));
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

    std::lock_guard lock(context.session_mutex);
    auto opened = open_requested_session(context.session, context.open_options, *command.session_id);
    if (!opened)
      return handled(write_error(context.output, command.id, opened.error()));
    context.session = std::move(*opened);
    reset_cancel_after_session_switch(context.run_state);
    return handled(write_success(context.output, command.id, state_result_json(context.session, false)));
  }

  return false;
}

}  // namespace ava::app::rpc
