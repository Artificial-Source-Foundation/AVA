#include "sys.h"
#include "ava/tools/diff_utils.h"
#include "ava/tools/edit_match.h"
#include "ava/tools/file_io.h"
#include "ava/tools/file_tools.h"
#include "ava/tools/mutation_queue.h"
#include "ava/tools/secure_workspace.h"
#include "ava/permissions/permission_rules.h"
#include "ava/core/ids.h"
#include "ava/core/json.h"
#include "ava/core/path.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <string_view>
#include <utility>

namespace ava::tools {

namespace {

constexpr std::size_t kMaxPermissionDiffBytes = 32 * 1024;

struct PermissionDiffPreview
{
  std::string text;
  bool truncated = false;
};

using detail::check_canceled;
using detail::is_canceled_error;
using detail::read_all_text;
using detail::read_head_text;
using detail::write_file_unlocked;

std::string effective_tool_name(ToolContext const& context, ava::permissions::Operation operation, std::string_view tool_name)
{
  if (!tool_name.empty())
    return std::string(tool_name);
  if (!context.permission_tool_name.empty())
    return context.permission_tool_name;
  return ava::permissions::to_string(operation);
}

ava::core::VoidResult record_permission_audit(ToolContext const& context, PermissionAuditEvent const& event)
{
  if (context.permission_request_ids && !event.permission_request_id.empty() &&
      std::ranges::find(*context.permission_request_ids, event.permission_request_id) == context.permission_request_ids->end())
  {
    context.permission_request_ids->push_back(event.permission_request_id);
  }
  if (!context.permission_audit_sink)
    return {};
  return context.permission_audit_sink(event);
}

PermissionAuditEvent audit_event(ToolContext const& context, std::string permission_request_id, ava::permissions::Operation operation, std::string tool_name,
                                 ava::permissions::PermissionDecision const& decision, std::filesystem::path const& target_path, std::string_view command)
{
  return PermissionAuditEvent{.permission_request_id = std::move(permission_request_id),
                              .operation = operation,
                              .mode = context.mode,
                              .tool_name = std::move(tool_name),
                              .action = decision.action,
                              .reason = decision.reason,
                              .risk = decision.risk,
                              .target_path = target_path,
                              .command = context.redact_permission_audit_arguments && !command.empty() ? std::string("[redacted]") : std::string(command),
                              .resolution = "",
                              .resolution_source = "policy",
                              .resolution_reason = "",
                              .actor = context.permission_actor.empty() ? std::string("agent") : context.permission_actor,
                              .rule_id = ""};
}

ava::core::Error permission_denied_error(std::string_view error_message, ava::permissions::PermissionDecision const& decision,
                                         std::filesystem::path const& target_path, std::string_view command, std::string_view resolution_context,
                                         std::string_view resolution_reason = {}, std::string_view permission_request_id = {})
{
  auto error = ava::core::Error(ava::core::ErrorCategory::PermissionDenied, std::string(error_message));
  error.with_context("action", ava::permissions::to_string(decision.action));
  error.with_context("reason", decision.reason);
  error.with_context("risk", ava::permissions::to_string(decision.risk));
  if (!permission_request_id.empty())
  {
    error.with_context("request_id", std::string(permission_request_id));
  }
  if (!command.empty())
  {
    error.with_context("command", std::string(command));
  }
  else
  {
    error.with_context("path", target_path.string());
  }
  if (decision.action == ava::permissions::PermissionAction::Ask)
  {
    error.with_context("resolution", std::string(resolution_context));
    if (!resolution_reason.empty())
      error.with_context("resolution_reason", std::string(resolution_reason));
    if (resolution_context == "no_resolver")
      error.with_context("headless_hint", "permission ask failed closed because no interactive or RPC resolver was available");
  }
  if (!permission_request_id.empty())
  {
    auto const id = std::string(permission_request_id);
    error.with_context("inspect", "/permissions audit show " + id);
    error.with_context("diagnose", "/permissions diagnose " + id);
  }
  return error;
}

std::shared_ptr<MutationQueue> effective_mutation_queue(ToolContext const& context)
{
  if (context.mutation_queue)
    return context.mutation_queue;
  return default_mutation_queue();
}

using ava::core::normalized_absolute_path;

void append_permission_rule_store_for_config_dir(std::vector<ava::permissions::PermissionRuleStore>& stores, ToolContext const& context,
                                                 std::filesystem::path const& config_dir)
{
  if (context.workspace_dir.empty() || config_dir.empty())
    return;
  stores.push_back(ava::permissions::PermissionRuleStore{.global_rules_file = config_dir / "permission-rules.json",
                                                         .workspace_rules_file = context.workspace_dir / ".ava" / "permission-rules.json",
                                                         .workspace_dir = context.workspace_dir});
}

std::vector<ava::permissions::PermissionRuleStore> context_permission_rule_stores(ToolContext const& context)
{
  std::vector<ava::permissions::PermissionRuleStore> stores;
  if (!context.mcp_global_config_file.empty())
  {
    append_permission_rule_store_for_config_dir(stores, context, context.mcp_global_config_file.parent_path());
  }
  if (!context.plugin_global_plugins_dir.empty())
  {
    append_permission_rule_store_for_config_dir(stores, context, context.plugin_global_plugins_dir.parent_path());
  }
  return stores;
}

bool is_legacy_workspace_permission_rules_path(ToolContext const& context, std::filesystem::path const& path)
{
  if (context.workspace_dir.empty())
    return false;
  auto const protected_path = normalized_absolute_path(context.workspace_dir / ".ava" / "permission-rules.json");
  return normalized_absolute_path(path) == protected_path;
}

bool is_enforceable_permission_rules_path(ToolContext const& context, std::filesystem::path const& path)
{
  if (ava::permissions::is_registered_enforceable_permission_rules_file(path))
    return true;
  for (auto const& store : context_permission_rule_stores(context))
  {
    if (ava::permissions::is_enforceable_permission_rules_file(store, path))
      return true;
  }
  return false;
}

ava::core::VoidResult reject_permission_rules_file_mutation(ToolContext const& context, std::filesystem::path const& path)
{
  if (!is_legacy_workspace_permission_rules_path(context, path) && !is_enforceable_permission_rules_path(context, path))
    return {};

  auto error = ava::core::Error(ava::core::ErrorCategory::PermissionDenied, "permission rule files cannot be modified by normal file tools");
  error.with_context("path", normalized_absolute_path(path).string());
  error.with_context("management", "use permission_rule_add or permission_rule_remove RPC commands");
  return std::unexpected(std::move(error));
}

ava::core::Result<std::optional<PermissionDiffPreview>> write_permission_diff_preview(ToolContext const& context, std::filesystem::path const& path,
                                                                                      std::string_view content)
{
  if (auto canceled = check_canceled(context, "write_file_permission_preview", path); !canceled)
  {
    return std::unexpected(std::move(canceled.error()));
  }
  // Permission must precede every installed frontend call. A local preview
  // would also be misleading when the frontend owns the exact written bytes.
  if (context.exact_file_access && context.exact_file_access->supports_write_text_file())
    return std::optional<PermissionDiffPreview>{};
  bool exists = false;
  if (context.secure_workspace)
  {
    auto resolved = context.secure_workspace->resolve(path, SecureWorkspaceResolveMode::AllowMissing);
    if (!resolved)
      return std::unexpected(std::move(resolved.error()));
    exists = resolved->exists;
  }
  else
  {
    std::error_code exists_error;
    exists = std::filesystem::exists(path, exists_error);
    if (exists_error)
      return std::optional<PermissionDiffPreview>{};
  }

  std::string original;
  if (exists)
  {
    auto const read_decision = ava::permissions::decide(ava::permissions::PermissionRequest{
        .operation = ava::permissions::Operation::ReadFile,
        .mode = context.mode,
        .workspace_dir = context.workspace_dir,
        .target_path = path,
        .command = "",
    });
    if (read_decision.action != ava::permissions::PermissionAction::Allow)
    {
      return std::optional<PermissionDiffPreview>{};
    }
    auto current = context.exact_file_access && !context.exact_file_access->supports_write_text_file()
                       ? detail::read_all_text_local_only(context, path, "write_file_permission_preview")
                       : read_all_text(context, path, "write_file_permission_preview");
    if (!current)
    {
      if (is_canceled_error(current.error()))
        return std::unexpected(std::move(current.error()));
      return std::optional<PermissionDiffPreview>{};
    }
    original = std::move(*current);
  }

  auto diff = unified_diff(original, content, path, path, kMaxPermissionDiffBytes);
  return std::optional<PermissionDiffPreview>{PermissionDiffPreview{.text = std::move(diff.text), .truncated = diff.truncated}};
}

ava::core::VoidResult announce_execution_start_impl(ToolContext const& context)
{
  if (!context.announce_execution_after_permission || !context.progress_sink || !context.execution_started)
    return {};
  if (context.execution_started->exchange(true, std::memory_order_acq_rel))
    return {};
  return context.progress_sink(ToolProgressEvent{
      .text = "authorized; execution starting", .call_id = context.current_call_id, .tool_name = context.current_tool_name, .status = "in_progress"});
}

}  // namespace

ava::core::VoidResult announce_tool_execution_start(ToolContext const& context)
{
  return announce_execution_start_impl(context);
}

ava::core::VoidResult ensure_permission(ToolContext const& context, ava::permissions::Operation operation, std::filesystem::path const& target_path,
                                        std::string_view command, std::string_view tool_name, std::string_view error_message, std::string_view diff_preview,
                                        bool diff_truncated)
{
  auto permission_target = target_path;
  if (context.secure_workspace && (operation == ava::permissions::Operation::ReadFile || operation == ava::permissions::Operation::EditFile ||
                                   operation == ava::permissions::Operation::SearchFiles))
  {
    auto const remote_read = context.exact_file_access && context.exact_file_access->supports_read_text_file();
    auto const mode = operation == ava::permissions::Operation::EditFile || (operation == ava::permissions::Operation::ReadFile && remote_read)
                          ? SecureWorkspaceResolveMode::AllowMissing
                          : SecureWorkspaceResolveMode::Existing;
    auto resolved = context.secure_workspace->resolve(target_path, mode);
    if (!resolved)
      return std::unexpected(std::move(resolved.error()));
    permission_target = std::move(resolved->absolute);
  }

  if (operation == ava::permissions::Operation::EditFile)
  {
    if (auto protected_file = reject_permission_rules_file_mutation(context, permission_target); !protected_file)
    {
      return protected_file;
    }
  }

  auto const request_tool_name = effective_tool_name(context, operation, tool_name);
  auto const permission_request_id = ava::core::make_id("permreq");
  auto decision = ava::permissions::decide(ava::permissions::PermissionRequest{
      .operation = operation,
      .mode = context.mode,
      .workspace_dir = context.workspace_dir,
      .target_path = permission_target,
      .command = std::string(command),
  });
  if (context.require_explicit_file_permissions && decision.action == ava::permissions::PermissionAction::Allow &&
      (operation == ava::permissions::Operation::ReadFile || operation == ava::permissions::Operation::EditFile))
  {
    decision.action = ava::permissions::PermissionAction::Ask;
    decision.reason = "The frontend requires explicit approval for model-initiated file access after backend safety checks";
    decision.risk = operation == ava::permissions::Operation::EditFile ? ava::permissions::PermissionRisk::Medium : ava::permissions::PermissionRisk::Low;
  }

  auto policy_event = audit_event(context, permission_request_id, operation, request_tool_name, decision, permission_target, command);
  if (decision.action == ava::permissions::PermissionAction::Allow || decision.action == ava::permissions::PermissionAction::Deny)
  {
    policy_event.resolution = ava::permissions::to_string(decision.action);
  }
  if (auto audited = record_permission_audit(context, policy_event); !audited)
  {
    return std::unexpected(std::move(audited.error()));
  }
  if (decision.action == ava::permissions::PermissionAction::Allow)
  {
    return {};
  }
  if (decision.action == ava::permissions::PermissionAction::Deny)
  {
    return std::unexpected(permission_denied_error(error_message, decision, permission_target, command, "policy", "", permission_request_id));
  }

  if (!context.permission_resolver)
  {
    auto outcome_event = policy_event;
    outcome_event.resolution = "deny";
    outcome_event.resolution_source = "no_resolver";
    if (auto audited = record_permission_audit(context, outcome_event); !audited)
    {
      return std::unexpected(std::move(audited.error()));
    }
    return std::unexpected(permission_denied_error(error_message, decision, permission_target, command, "no_resolver", "", permission_request_id));
  }

  auto resolution = context.permission_resolver(ava::permissions::PermissionPrompt{
      .permission_request_id = permission_request_id,
      .tool_call_id = context.current_call_id,
      .operation = operation,
      .mode = context.mode,
      .workspace_dir = context.workspace_dir,
      .target_path = permission_target,
      .command = std::string(command),
      .tool_name = request_tool_name,
      .reason = decision.reason,
      .risk = decision.risk,
      .diff_preview = std::string(diff_preview),
      .diff_truncated = diff_truncated,
  });

  auto outcome_event = policy_event;
  outcome_event.resolution_source =
      resolution && !resolution->resolution_source.empty()
          ? resolution->resolution_source
          : (resolution && *resolution == ava::permissions::PermissionResolution::AllowSessionGrant ? "session_grant" : "resolver");
  if (!resolution)
    outcome_event.resolution_source = "resolver_failed";
  outcome_event.resolution = resolution ? ava::permissions::to_string(*resolution) : "deny";
  if (resolution)
    outcome_event.resolution_reason = resolution->reason;
  else
    outcome_event.resolution_reason = resolution.error().format();
  if (resolution)
    outcome_event.rule_id = resolution->rule_id;
  if (auto audited = record_permission_audit(context, outcome_event); !audited)
  {
    return std::unexpected(std::move(audited.error()));
  }
  if (resolution && (*resolution == ava::permissions::PermissionResolution::Allow || *resolution == ava::permissions::PermissionResolution::AllowSessionGrant))
  {
    return {};
  }
  if (resolution && *resolution == ava::permissions::PermissionResolution::Cancel)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::Unknown, "agent loop canceled");
    error.with_context("permission_request_id", permission_request_id);
    error.with_context("resolution_reason", resolution->reason);
    return std::unexpected(std::move(error));
  }

  auto const resolution_context = resolution ? ava::permissions::to_string(*resolution) : std::string("resolver_failed");
  auto const resolution_reason = resolution ? resolution->reason : resolution.error().format();
  return std::unexpected(
      permission_denied_error(error_message, decision, permission_target, command, resolution_context, resolution_reason, permission_request_id));
}

std::string permission_audit_data_json(PermissionAuditEvent const& event)
{
  std::string data = "{";
  if (!event.permission_request_id.empty())
  {
    data += "\"permission_request_id\":\"" + ava::core::json::escape(event.permission_request_id) + "\",";
  }
  data += "\"operation\":\"" + ava::core::json::escape(ava::permissions::to_string(event.operation)) + "\",\"mode\":\"" +
          ava::core::json::escape(ava::agent::to_string(event.mode)) + "\",\"tool_name\":\"" + ava::core::json::escape(event.tool_name) + "\",\"action\":\"" +
          ava::core::json::escape(ava::permissions::to_string(event.action)) + "\",\"reason\":\"" + ava::core::json::escape(event.reason) + "\",\"risk\":\"" +
          ava::core::json::escape(ava::permissions::to_string(event.risk)) + "\"";
  if (event.operation != ava::permissions::Operation::RunCommand && event.operation != ava::permissions::Operation::NetworkFetch && !event.target_path.empty())
  {
    data += ",\"target_path\":\"" + ava::core::json::escape(event.target_path.string()) + "\"";
  }
  if (!event.command.empty())
  {
    data += ",\"command\":\"" + ava::core::json::escape(event.command) + "\"";
  }
  if (!event.resolution.empty())
  {
    data += ",\"resolution\":\"" + ava::core::json::escape(event.resolution) + "\"";
  }
  if (!event.resolution_source.empty())
  {
    data += ",\"resolution_source\":\"" + ava::core::json::escape(event.resolution_source) + "\"";
  }
  if (!event.resolution_reason.empty())
  {
    data += ",\"resolution_reason\":\"" + ava::core::json::escape(event.resolution_reason) + "\"";
  }
  if (!event.actor.empty())
  {
    data += ",\"actor\":\"" + ava::core::json::escape(event.actor) + "\"";
  }
  if (!event.rule_id.empty())
  {
    data += ",\"rule_id\":\"" + ava::core::json::escape(event.rule_id) + "\"";
  }
  data += '}';
  return data;
}

ava::core::Result<TextOutput> read_file(ToolContext const& context, std::filesystem::path const& path, ReadOptions options)
{
  if (auto canceled = check_canceled(context, "read_file", path); !canceled)
  {
    return std::unexpected(std::move(canceled.error()));
  }
  if (!options.permission_already_checked)
  {
    if (auto permission = ensure_permission(context, ava::permissions::Operation::ReadFile, path, "", "", "tool requires permission"); !permission)
    {
      return std::unexpected(permission.error());
    }
  }
  if (auto canceled = check_canceled(context, "read_file", path); !canceled)
  {
    return std::unexpected(std::move(canceled.error()));
  }
  if (!options.permission_already_checked)
  {
    if (auto started = announce_tool_execution_start(context); !started)
      return std::unexpected(std::move(started.error()));
  }

  return read_head_text(context, path, options);
}

ava::core::Result<FileMutationResult> write_file(ToolContext const& context, std::filesystem::path const& path, std::string_view content, WriteOptions options)
{
  if (auto canceled = check_canceled(context, "write_file", path); !canceled)
  {
    return std::unexpected(std::move(canceled.error()));
  }
  // Reject symlinks before any permission check or file read. This prevents
  // writing through a symlink to a protected target (e.g., a source file in
  // plan mode) without needing canonicalization to detect the target.
  // The check uses symlink_status (no path resolution), consistent with
  // read_file's symlink rejection.
  if (std::error_code link_error; std::filesystem::is_symlink(std::filesystem::symlink_status(path, link_error)))
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::PermissionDenied, "file writes do not follow symlinks");
    error.with_context("operation", "write_file");
    error.with_context("path", path.string());
    return std::unexpected(std::move(error));
  }
  if (auto protected_file = reject_permission_rules_file_mutation(context, path); !protected_file)
  {
    return std::unexpected(std::move(protected_file.error()));
  }
  std::optional<PermissionDiffPreview> preview;
  if (!options.permission_already_checked)
  {
    auto preview_result = write_permission_diff_preview(context, path, content);
    if (!preview_result)
      return std::unexpected(std::move(preview_result.error()));
    preview = std::move(*preview_result);
    auto const diff_preview = preview ? std::string_view(preview->text) : std::string_view{};
    if (auto permission = ensure_permission(context, ava::permissions::Operation::EditFile, path, "", "", "tool requires permission", diff_preview,
                                            preview ? preview->truncated : false);
        !permission)
    {
      return std::unexpected(permission.error());
    }
  }
  if (auto canceled = check_canceled(context, "write_file", path); !canceled)
  {
    return std::unexpected(std::move(canceled.error()));
  }
  if (!options.permission_already_checked)
  {
    if (auto started = announce_tool_execution_start(context); !started)
      return std::unexpected(std::move(started.error()));
  }

  auto attach_preview = [&preview](ava::core::Result<FileMutationResult> written) -> ava::core::Result<FileMutationResult> {
    if (written && preview)
    {
      written->diff = std::move(preview->text);
      written->diff_truncated = preview->truncated;
    }
    return written;
  };

  if (options.mutation_already_locked)
    return attach_preview(write_file_unlocked(context, path, content));
  [[maybe_unused]] auto mutation_lock = effective_mutation_queue(context)->lock_path(path);
  return attach_preview(write_file_unlocked(context, path, content));
}

ava::core::Result<FileMutationResult> edit_file(ToolContext const& context, std::filesystem::path const& path, std::string_view old_text,
                                                std::string_view new_text)
{
  if (old_text.empty())
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "old_text must not be empty");
    error.with_context("path", path.string());
    return std::unexpected(std::move(error));
  }
  if (context.exact_file_access && context.exact_file_access->supports_read_text_file() != context.exact_file_access->supports_write_text_file())
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::PermissionDenied,
                                  "edit_file requires coherent read and write ownership; the exact-file capabilities are partial");
    error.with_context("path", path.string());
    return std::unexpected(std::move(error));
  }
  if (auto canceled = check_canceled(context, "edit_file", path); !canceled)
  {
    return std::unexpected(std::move(canceled.error()));
  }
  if (auto protected_file = reject_permission_rules_file_mutation(context, path); !protected_file)
  {
    return std::unexpected(std::move(protected_file.error()));
  }
  auto const read_decision = ava::permissions::decide(ava::permissions::PermissionRequest{
      .operation = ava::permissions::Operation::ReadFile,
      .mode = context.mode,
      .workspace_dir = context.workspace_dir,
      .target_path = path,
      .command = "",
  });
  if (read_decision.action == ava::permissions::PermissionAction::Deny)
  {
    if (auto permission = ensure_permission(context, ava::permissions::Operation::ReadFile, path, "", "", "tool requires permission"); !permission)
    {
      return std::unexpected(permission.error());
    }
  }
  auto const edit_decision = ava::permissions::decide(ava::permissions::PermissionRequest{
      .operation = ava::permissions::Operation::EditFile,
      .mode = context.mode,
      .workspace_dir = context.workspace_dir,
      .target_path = path,
      .command = "",
  });
  if (edit_decision.action == ava::permissions::PermissionAction::Deny)
  {
    if (auto permission = ensure_permission(context, ava::permissions::Operation::EditFile, path, "", "", "tool requires permission"); !permission)
    {
      return std::unexpected(permission.error());
    }
  }
  if (auto permission = ensure_permission(context, ava::permissions::Operation::ReadFile, path, "", "", "tool requires permission"); !permission)
  {
    return std::unexpected(permission.error());
  }
  if (auto canceled = check_canceled(context, "edit_file", path); !canceled)
  {
    return std::unexpected(std::move(canceled.error()));
  }

  [[maybe_unused]] auto mutation_lock = effective_mutation_queue(context)->lock_path(path);
  auto content = read_all_text(context, path, "edit_file");
  if (!content)
  {
    return std::unexpected(content.error());
  }
  if (auto canceled = check_canceled(context, "edit_file", path); !canceled)
  {
    return std::unexpected(std::move(canceled.error()));
  }

  auto match = find_unique_text_match(*content, old_text, path, "old_text was not found", "old_text is not unique");
  if (!match)
    return std::unexpected(match.error());

  auto const original = *content;
  content->replace(match->position, match->size, new_text);
  auto diff = unified_diff(original, *content, path, path, kMaxPermissionDiffBytes);
  if (auto permission = ensure_permission(context, ava::permissions::Operation::EditFile, path, "", "", "tool requires permission", diff.text, diff.truncated);
      !permission)
  {
    return std::unexpected(permission.error());
  }
  if (auto canceled = check_canceled(context, "edit_file", path); !canceled)
  {
    return std::unexpected(std::move(canceled.error()));
  }
  if (auto started = announce_tool_execution_start(context); !started)
    return std::unexpected(std::move(started.error()));
  auto written = write_file(context, path, *content, WriteOptions{.permission_already_checked = true, .mutation_already_locked = true});
  if (!written)
    return std::unexpected(written.error());

  written->diff = std::move(diff.text);
  written->diff_truncated = diff.truncated;
  written->line_endings = to_string(match->content_analysis.line_endings);
  written->had_utf8_bom = match->content_analysis.has_utf8_bom;
  return written;
}

}  // namespace ava::tools
