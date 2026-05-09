#include "ava/tools/diff_utils.h"
#include "ava/tools/edit_match.h"
#include "ava/tools/file_io.h"
#include "ava/tools/file_tools.h"
#include "ava/tools/mutation_queue.h"
#include "ava/core/ids.h"
#include "ava/core/json.h"

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
                              .command = std::string(command),
                              .resolution = "",
                              .resolution_source = "policy",
                              .resolution_reason = ""};
}

ava::core::Error permission_denied_error(std::string_view error_message, ava::permissions::PermissionDecision const& decision,
                                         std::filesystem::path const& target_path, std::string_view command, std::string_view resolution_context,
                                         std::string_view resolution_reason = {})
{
  auto error = ava::core::Error(ava::core::ErrorCategory::PermissionDenied, std::string(error_message));
  error.with_context("action", ava::permissions::to_string(decision.action));
  error.with_context("reason", decision.reason);
  error.with_context("risk", ava::permissions::to_string(decision.risk));
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
  }
  return error;
}

std::shared_ptr<MutationQueue> effective_mutation_queue(ToolContext const& context)
{
  if (context.mutation_queue)
    return context.mutation_queue;
  return default_mutation_queue();
}

ava::core::Result<std::optional<PermissionDiffPreview>> write_permission_diff_preview(ToolContext const& context, std::filesystem::path const& path,
                                                                                      std::string_view content)
{
  if (auto canceled = check_canceled(context, "write_file_permission_preview", path); !canceled)
  {
    return std::unexpected(std::move(canceled.error()));
  }
  std::error_code exists_error;
  bool const exists = std::filesystem::exists(path, exists_error);
  if (exists_error)
    return std::optional<PermissionDiffPreview>{};

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
    auto current = read_all_text(context, path, "write_file_permission_preview");
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

}  // namespace

ava::core::VoidResult ensure_permission(ToolContext const& context, ava::permissions::Operation operation, std::filesystem::path const& target_path,
                                        std::string_view command, std::string_view tool_name, std::string_view error_message, std::string_view diff_preview,
                                        bool diff_truncated)
{
  auto const request_tool_name = effective_tool_name(context, operation, tool_name);
  auto const permission_request_id = ava::core::make_id("permreq");
  auto const decision = ava::permissions::decide(ava::permissions::PermissionRequest{
      .operation = operation,
      .mode = context.mode,
      .workspace_dir = context.workspace_dir,
      .target_path = target_path,
      .command = std::string(command),
  });

  auto policy_event = audit_event(context, permission_request_id, operation, request_tool_name, decision, target_path, command);
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
    return std::unexpected(permission_denied_error(error_message, decision, target_path, command, "policy"));
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
    return std::unexpected(permission_denied_error(error_message, decision, target_path, command, "no_resolver"));
  }

  auto resolution = context.permission_resolver(ava::permissions::PermissionPrompt{
      .permission_request_id = permission_request_id,
      .operation = operation,
      .mode = context.mode,
      .workspace_dir = context.workspace_dir,
      .target_path = target_path,
      .command = std::string(command),
      .tool_name = request_tool_name,
      .reason = decision.reason,
      .risk = decision.risk,
      .diff_preview = std::string(diff_preview),
      .diff_truncated = diff_truncated,
  });

  auto outcome_event = policy_event;
  outcome_event.resolution_source = resolution && *resolution == ava::permissions::PermissionResolution::AllowSessionGrant ? "session_grant" : "resolver";
  if (!resolution)
    outcome_event.resolution_source = "resolver_failed";
  outcome_event.resolution = resolution ? ava::permissions::to_string(*resolution) : "deny";
  if (resolution)
    outcome_event.resolution_reason = resolution->reason;
  if (auto audited = record_permission_audit(context, outcome_event); !audited)
  {
    return std::unexpected(std::move(audited.error()));
  }
  if (resolution && (*resolution == ava::permissions::PermissionResolution::Allow || *resolution == ava::permissions::PermissionResolution::AllowSessionGrant))
  {
    return {};
  }

  auto const resolution_context = resolution ? ava::permissions::to_string(*resolution) : std::string("resolver_failed");
  auto const resolution_reason = resolution ? std::string_view(resolution->reason) : std::string_view{};
  return std::unexpected(permission_denied_error(error_message, decision, target_path, command, resolution_context, resolution_reason));
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

  return read_head_text(context, path, options);
}

ava::core::Result<FileMutationResult> write_file(ToolContext const& context, std::filesystem::path const& path, std::string_view content, WriteOptions options)
{
  if (auto canceled = check_canceled(context, "write_file", path); !canceled)
  {
    return std::unexpected(std::move(canceled.error()));
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

  if (options.mutation_already_locked)
    return write_file_unlocked(context, path, content);
  [[maybe_unused]] auto mutation_lock = effective_mutation_queue(context)->lock_path(path);
  return write_file_unlocked(context, path, content);
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
  if (auto canceled = check_canceled(context, "edit_file", path); !canceled)
  {
    return std::unexpected(std::move(canceled.error()));
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
