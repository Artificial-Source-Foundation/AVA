#include "ava/tools/file_tools.h"

#include <array>
#include <cerrno>
#include <fstream>
#include <memory>
#include <optional>
#include <string_view>
#include <system_error>
#include <utility>

#include "ava/core/ids.h"
#include "ava/core/json.h"
#include "ava/tools/diff_utils.h"
#include "ava/tools/edit_match.h"
#include "ava/tools/mutation_queue.h"

namespace ava::tools {

namespace {

constexpr std::size_t kMaxPermissionDiffBytes = 32 * 1024;

struct PermissionDiffPreview {
  std::string text;
  bool truncated = false;
};

std::string effective_tool_name(ToolContext const& context, ava::permissions::Operation operation,
                                std::string_view tool_name)
{
  if (!tool_name.empty()) return std::string(tool_name);
  if (!context.permission_tool_name.empty()) return context.permission_tool_name;
  return ava::permissions::to_string(operation);
}

ava::core::VoidResult record_permission_audit(ToolContext const& context, PermissionAuditEvent const& event)
{
  if (!context.permission_audit_sink) return {};
  return context.permission_audit_sink(event);
}

PermissionAuditEvent audit_event(ToolContext const& context, ava::permissions::Operation operation,
                                 std::string tool_name, ava::permissions::PermissionDecision const& decision,
                                 std::filesystem::path const& target_path, std::string_view command)
{
  return PermissionAuditEvent{.operation = operation,
                              .mode = context.mode,
                              .tool_name = std::move(tool_name),
                              .action = decision.action,
                              .reason = decision.reason,
                              .target_path = target_path,
                              .command = std::string(command),
                              .resolution = "",
                              .resolution_source = "policy"};
}

ava::core::Error permission_denied_error(std::string_view error_message,
                                         ava::permissions::PermissionDecision const& decision,
                                         std::filesystem::path const& target_path, std::string_view command,
                                         std::string_view resolution_context)
{
  auto error = ava::core::Error(ava::core::ErrorCategory::PermissionDenied, std::string(error_message));
  error.with_context("action", ava::permissions::to_string(decision.action));
  error.with_context("reason", decision.reason);
  if (!command.empty()) {
    error.with_context("command", std::string(command));
  } else {
    error.with_context("path", target_path.string());
  }
  if (decision.action == ava::permissions::PermissionAction::Ask) {
    error.with_context("resolution", std::string(resolution_context));
  }
  return error;
}

bool is_canceled(ToolContext const& context)
{
  return context.cancel_requested && context.cancel_requested();
}

ava::core::Error canceled_error(std::string_view operation, std::filesystem::path const& path)
{
  auto error = ava::core::Error(ava::core::ErrorCategory::Unknown, "tool canceled");
  error.with_context("operation", std::string(operation));
  if (!path.empty()) error.with_context("path", path.string());
  return error;
}

ava::core::VoidResult check_canceled(ToolContext const& context, std::string_view operation,
                                     std::filesystem::path const& path = {})
{
  if (!is_canceled(context)) return {};
  return std::unexpected(canceled_error(operation, path));
}

bool is_canceled_error(ava::core::Error const& error)
{
  return error.message().find("canceled") != std::string::npos;
}

ava::core::Result<std::string> read_all_text(ToolContext const& context, std::filesystem::path const& path,
                                             std::string_view operation)
{
  if (auto canceled = check_canceled(context, operation, path); !canceled) {
    return std::unexpected(std::move(canceled.error()));
  }
  constexpr std::size_t max_edit_file_bytes = 10 * 1024 * 1024;
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to open file for reading");
    error.with_context("path", path.string());
    return std::unexpected(std::move(error));
  }
  std::string content;
  content.reserve(4096);
  std::array<char, 4096> buffer{};
  while (file) {
    if (auto canceled = check_canceled(context, operation, path); !canceled) {
      return std::unexpected(std::move(canceled.error()));
    }
    file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    auto const count = file.gcount();
    if (count > 0) {
      if (content.size() + static_cast<std::size_t>(count) > max_edit_file_bytes) {
        auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "file is too large for exact edit");
        error.with_context("path", path.string());
        error.with_context("max_bytes", std::to_string(max_edit_file_bytes));
        return std::unexpected(std::move(error));
      }
      content.append(buffer.data(), static_cast<std::size_t>(count));
    }
  }
  if (auto canceled = check_canceled(context, operation, path); !canceled) {
    return std::unexpected(std::move(canceled.error()));
  }
  if (!file.eof() && file.fail()) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed while reading file");
    error.with_context("path", path.string());
    return std::unexpected(std::move(error));
  }
  return content;
}

ava::core::Result<TextOutput> read_head_text(ToolContext const& context, std::filesystem::path const& path,
                                             std::size_t max_bytes)
{
  if (auto canceled = check_canceled(context, "read_file", path); !canceled) {
    return std::unexpected(std::move(canceled.error()));
  }
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to open file for reading");
    error.with_context("path", path.string());
    return std::unexpected(std::move(error));
  }

  TextOutput output;
  std::array<char, 4096> buffer{};
  while (file) {
    if (auto canceled = check_canceled(context, "read_file", path); !canceled) {
      return std::unexpected(std::move(canceled.error()));
    }
    file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    auto const count = static_cast<std::size_t>(file.gcount());
    if (count == 0) {
      continue;
    }
    output.total_bytes += count;
    if (output.content.size() < max_bytes) {
      auto const remaining = max_bytes - output.content.size();
      output.content.append(buffer.data(), std::min(remaining, count));
    }
  }
  if (auto canceled = check_canceled(context, "read_file", path); !canceled) {
    return std::unexpected(std::move(canceled.error()));
  }
  if (!file.eof() && file.fail()) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed while reading file");
    error.with_context("path", path.string());
    return std::unexpected(std::move(error));
  }
  output.output_bytes = output.content.size();
  output.truncated = output.output_bytes < output.total_bytes;
  return output;
}

std::filesystem::path write_parent_path(std::filesystem::path const& path)
{
  auto const parent = path.parent_path();
  if (!parent.empty()) return parent;
  return ".";
}

std::filesystem::path unique_write_temp_path(std::filesystem::path const& target)
{
  auto const parent = write_parent_path(target);
  auto const filename = target.filename().empty() ? std::string("file") : target.filename().string();
  auto const stem = "." + filename + ".ava-write-";
  for (int attempt = 0; attempt < 8; ++attempt) {
    auto candidate = parent / (stem + ava::core::make_id("tmp") + ".tmp");
    std::error_code exists_error;
    if (!std::filesystem::exists(candidate, exists_error) && !exists_error) return candidate;
  }
  return parent / (stem + ava::core::make_id("tmp") + ".tmp");
}

ava::core::Error io_error(std::string message, std::filesystem::path const& path, std::string cause)
{
  auto error = ava::core::Error(ava::core::ErrorCategory::Io, std::move(message));
  error.with_context("path", path.string());
  if (!cause.empty()) error.with_context("cause", std::move(cause));
  return error;
}

ava::core::Error staged_io_error(std::string message, std::filesystem::path const& target_path,
                                 std::filesystem::path const& temp_path, std::string cause)
{
  auto error = ava::core::Error(ava::core::ErrorCategory::Io, std::move(message));
  error.with_context("path", target_path.string());
  error.with_context("temp_path", temp_path.string());
  if (!cause.empty()) error.with_context("cause", std::move(cause));
  return error;
}

std::string errno_cause(int value)
{
  if (value == 0) return "stream operation failed";
  return std::generic_category().message(value);
}

std::shared_ptr<MutationQueue> effective_mutation_queue(ToolContext const& context)
{
  if (context.mutation_queue) return context.mutation_queue;
  return default_mutation_queue();
}

ava::core::Result<std::optional<PermissionDiffPreview>> write_permission_diff_preview(ToolContext const& context,
                                                                                      std::filesystem::path const& path,
                                                                                      std::string_view content)
{
  if (auto canceled = check_canceled(context, "write_file_permission_preview", path); !canceled) {
    return std::unexpected(std::move(canceled.error()));
  }
  std::error_code exists_error;
  bool const exists = std::filesystem::exists(path, exists_error);
  if (exists_error) return std::optional<PermissionDiffPreview>{};

  std::string original;
  if (exists) {
    auto const read_decision = ava::permissions::decide(ava::permissions::PermissionRequest{
        .operation = ava::permissions::Operation::ReadFile,
        .mode = context.mode,
        .workspace_dir = context.workspace_dir,
        .target_path = path,
        .command = "",
    });
    if (read_decision.action != ava::permissions::PermissionAction::Allow) {
      return std::optional<PermissionDiffPreview>{};
    }
    auto current = read_all_text(context, path, "write_file_permission_preview");
    if (!current) {
      if (is_canceled_error(current.error())) return std::unexpected(std::move(current.error()));
      return std::optional<PermissionDiffPreview>{};
    }
    original = std::move(*current);
  }

  auto diff = unified_diff(original, content, path, path, kMaxPermissionDiffBytes);
  return std::optional<PermissionDiffPreview>{
      PermissionDiffPreview{.text = std::move(diff.text), .truncated = diff.truncated}};
}

ava::core::Result<FileMutationResult> write_file_unlocked(ToolContext const& context, std::filesystem::path const& path,
                                                          std::string_view content)
{
  if (auto canceled = check_canceled(context, "write_file", path); !canceled) {
    return std::unexpected(std::move(canceled.error()));
  }
  auto const parent_path = path.parent_path();
  if (!parent_path.empty()) {
    std::error_code mkdir_error;
    std::filesystem::create_directories(parent_path, mkdir_error);
    if (mkdir_error) {
      return std::unexpected(io_error("failed to create parent directory", parent_path, mkdir_error.message()));
    }
  }
  if (auto canceled = check_canceled(context, "write_file", path); !canceled) {
    return std::unexpected(std::move(canceled.error()));
  }

  std::error_code status_error;
  auto const existing_status = std::filesystem::status(path, status_error);
  bool const preserve_permissions = !status_error && std::filesystem::exists(existing_status);
  auto const existing_permissions = existing_status.permissions();

  auto const temp_path = unique_write_temp_path(path);
  if (auto canceled = check_canceled(context, "write_file", path); !canceled) {
    return std::unexpected(std::move(canceled.error()));
  }
  errno = 0;
  std::ofstream file(temp_path, std::ios::binary | std::ios::trunc);
  if (!file) {
    return std::unexpected(
        staged_io_error("failed to open temporary file for writing", path, temp_path, errno_cause(errno)));
  }
  errno = 0;
  constexpr std::size_t kWriteChunkBytes = 4096;
  std::size_t offset = 0;
  while (offset < content.size()) {
    if (auto canceled = check_canceled(context, "write_file", path); !canceled) {
      file.close();
      remove_staged_file_best_effort(temp_path);
      return std::unexpected(std::move(canceled.error()));
    }
    auto const count = std::min(kWriteChunkBytes, content.size() - offset);
    file.write(content.data() + offset, static_cast<std::streamsize>(count));
    if (!file) {
      file.close();
      remove_staged_file_best_effort(temp_path);
      return std::unexpected(staged_io_error("failed to write temporary file", path, temp_path, errno_cause(errno)));
    }
    offset += count;
  }
  if (auto canceled = check_canceled(context, "write_file", path); !canceled) {
    file.close();
    remove_staged_file_best_effort(temp_path);
    return std::unexpected(std::move(canceled.error()));
  }
  errno = 0;
  file.close();
  if (!file) {
    remove_staged_file_best_effort(temp_path);
    return std::unexpected(
        staged_io_error("failed to close temporary file after writing", path, temp_path, errno_cause(errno)));
  }

  if (auto canceled = check_canceled(context, "write_file", path); !canceled) {
    remove_staged_file_best_effort(temp_path);
    return std::unexpected(std::move(canceled.error()));
  }
  if (preserve_permissions) {
    std::error_code permissions_error;
    std::filesystem::permissions(temp_path, existing_permissions, std::filesystem::perm_options::replace,
                                 permissions_error);
    if (permissions_error) {
      remove_staged_file_best_effort(temp_path);
      return std::unexpected(staged_io_error("failed to apply target permissions to temporary file", path, temp_path,
                                             permissions_error.message()));
    }
  } else {
    std::error_code permissions_error;
    std::filesystem::permissions(temp_path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                                 std::filesystem::perm_options::replace, permissions_error);
    if (permissions_error) {
      remove_staged_file_best_effort(temp_path);
      return std::unexpected(staged_io_error("failed to apply new file permissions to temporary file", path, temp_path,
                                             permissions_error.message()));
    }
  }

  if (auto canceled = check_canceled(context, "write_file", path); !canceled) {
    remove_staged_file_best_effort(temp_path);
    return std::unexpected(std::move(canceled.error()));
  }
  if (auto committed = replace_file_with_staged_file(temp_path, path); !committed) {
    remove_staged_file_best_effort(temp_path);
    return std::unexpected(std::move(committed.error()));
  }

  FileMutationResult result;
  result.path = path;
  result.bytes_written = content.size();
  return result;
}

}  // namespace

ava::core::VoidResult ensure_permission(ToolContext const& context, ava::permissions::Operation operation,
                                        std::filesystem::path const& target_path, std::string_view command,
                                        std::string_view tool_name, std::string_view error_message,
                                        std::string_view diff_preview, bool diff_truncated)
{
  auto const request_tool_name = effective_tool_name(context, operation, tool_name);
  auto const decision = ava::permissions::decide(ava::permissions::PermissionRequest{
      .operation = operation,
      .mode = context.mode,
      .workspace_dir = context.workspace_dir,
      .target_path = target_path,
      .command = std::string(command),
  });

  auto policy_event = audit_event(context, operation, request_tool_name, decision, target_path, command);
  if (decision.action == ava::permissions::PermissionAction::Allow ||
      decision.action == ava::permissions::PermissionAction::Deny) {
    policy_event.resolution = ava::permissions::to_string(decision.action);
  }
  if (auto audited = record_permission_audit(context, policy_event); !audited) {
    return std::unexpected(std::move(audited.error()));
  }
  if (decision.action == ava::permissions::PermissionAction::Allow) {
    return {};
  }
  if (decision.action == ava::permissions::PermissionAction::Deny) {
    return std::unexpected(permission_denied_error(error_message, decision, target_path, command, "policy"));
  }

  if (!context.permission_resolver) {
    auto outcome_event = policy_event;
    outcome_event.resolution = "deny";
    outcome_event.resolution_source = "no_resolver";
    if (auto audited = record_permission_audit(context, outcome_event); !audited) {
      return std::unexpected(std::move(audited.error()));
    }
    return std::unexpected(permission_denied_error(error_message, decision, target_path, command, "no_resolver"));
  }

  auto resolution = context.permission_resolver(ava::permissions::PermissionPrompt{
      .operation = operation,
      .mode = context.mode,
      .workspace_dir = context.workspace_dir,
      .target_path = target_path,
      .command = std::string(command),
      .tool_name = request_tool_name,
      .reason = decision.reason,
      .diff_preview = std::string(diff_preview),
      .diff_truncated = diff_truncated,
  });

  auto outcome_event = policy_event;
  outcome_event.resolution_source = resolution ? "resolver" : "resolver_failed";
  outcome_event.resolution = resolution ? ava::permissions::to_string(*resolution) : "deny";
  if (auto audited = record_permission_audit(context, outcome_event); !audited) {
    return std::unexpected(std::move(audited.error()));
  }
  if (resolution && *resolution == ava::permissions::PermissionResolution::Allow) {
    return {};
  }

  auto const resolution_context =
      resolution ? ava::permissions::to_string(*resolution) : std::string("resolver_failed");
  return std::unexpected(permission_denied_error(error_message, decision, target_path, command, resolution_context));
}

std::string permission_audit_data_json(PermissionAuditEvent const& event)
{
  std::string data = "{\"operation\":\"" + ava::core::json::escape(ava::permissions::to_string(event.operation)) +
                     "\",\"mode\":\"" + ava::core::json::escape(ava::agent::to_string(event.mode)) +
                     "\",\"tool_name\":\"" + ava::core::json::escape(event.tool_name) + "\",\"action\":\"" +
                     ava::core::json::escape(ava::permissions::to_string(event.action)) + "\",\"reason\":\"" +
                     ava::core::json::escape(event.reason) + "\"";
  if (event.operation != ava::permissions::Operation::RunCommand &&
      event.operation != ava::permissions::Operation::NetworkFetch && !event.target_path.empty()) {
    data += ",\"target_path\":\"" + ava::core::json::escape(event.target_path.string()) + "\"";
  }
  if (!event.command.empty()) {
    data += ",\"command\":\"" + ava::core::json::escape(event.command) + "\"";
  }
  if (!event.resolution.empty()) {
    data += ",\"resolution\":\"" + ava::core::json::escape(event.resolution) + "\"";
  }
  if (!event.resolution_source.empty()) {
    data += ",\"resolution_source\":\"" + ava::core::json::escape(event.resolution_source) + "\"";
  }
  data += '}';
  return data;
}

ava::core::Result<TextOutput> read_file(ToolContext const& context, std::filesystem::path const& path,
                                        ReadOptions options)
{
  if (auto canceled = check_canceled(context, "read_file", path); !canceled) {
    return std::unexpected(std::move(canceled.error()));
  }
  if (!options.permission_already_checked) {
    if (auto permission =
            ensure_permission(context, ava::permissions::Operation::ReadFile, path, "", "", "tool requires permission");
        !permission) {
      return std::unexpected(permission.error());
    }
  }
  if (auto canceled = check_canceled(context, "read_file", path); !canceled) {
    return std::unexpected(std::move(canceled.error()));
  }

  return read_head_text(context, path, options.max_bytes);
}

ava::core::Result<FileMutationResult> write_file(ToolContext const& context, std::filesystem::path const& path,
                                                 std::string_view content, WriteOptions options)
{
  if (auto canceled = check_canceled(context, "write_file", path); !canceled) {
    return std::unexpected(std::move(canceled.error()));
  }
  std::optional<PermissionDiffPreview> preview;
  if (!options.permission_already_checked) {
    auto preview_result = write_permission_diff_preview(context, path, content);
    if (!preview_result) return std::unexpected(std::move(preview_result.error()));
    preview = std::move(*preview_result);
    auto const diff_preview = preview ? std::string_view(preview->text) : std::string_view{};
    if (auto permission =
            ensure_permission(context, ava::permissions::Operation::EditFile, path, "", "", "tool requires permission",
                              diff_preview, preview ? preview->truncated : false);
        !permission) {
      return std::unexpected(permission.error());
    }
  }
  if (auto canceled = check_canceled(context, "write_file", path); !canceled) {
    return std::unexpected(std::move(canceled.error()));
  }

  if (options.mutation_already_locked) return write_file_unlocked(context, path, content);
  [[maybe_unused]] auto mutation_lock = effective_mutation_queue(context)->lock_path(path);
  return write_file_unlocked(context, path, content);
}

ava::core::Result<FileMutationResult> edit_file(ToolContext const& context, std::filesystem::path const& path,
                                                std::string_view old_text, std::string_view new_text)
{
  if (old_text.empty()) {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "old_text must not be empty");
    error.with_context("path", path.string());
    return std::unexpected(std::move(error));
  }
  if (auto canceled = check_canceled(context, "edit_file", path); !canceled) {
    return std::unexpected(std::move(canceled.error()));
  }
  auto const read_decision = ava::permissions::decide(ava::permissions::PermissionRequest{
      .operation = ava::permissions::Operation::ReadFile,
      .mode = context.mode,
      .workspace_dir = context.workspace_dir,
      .target_path = path,
      .command = "",
  });
  if (read_decision.action == ava::permissions::PermissionAction::Deny) {
    if (auto permission =
            ensure_permission(context, ava::permissions::Operation::ReadFile, path, "", "", "tool requires permission");
        !permission) {
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
  if (edit_decision.action == ava::permissions::PermissionAction::Deny) {
    if (auto permission =
            ensure_permission(context, ava::permissions::Operation::EditFile, path, "", "", "tool requires permission");
        !permission) {
      return std::unexpected(permission.error());
    }
  }
  if (auto permission =
          ensure_permission(context, ava::permissions::Operation::ReadFile, path, "", "", "tool requires permission");
      !permission) {
    return std::unexpected(permission.error());
  }
  if (auto canceled = check_canceled(context, "edit_file", path); !canceled) {
    return std::unexpected(std::move(canceled.error()));
  }

  [[maybe_unused]] auto mutation_lock = effective_mutation_queue(context)->lock_path(path);
  auto content = read_all_text(context, path, "edit_file");
  if (!content) {
    return std::unexpected(content.error());
  }
  if (auto canceled = check_canceled(context, "edit_file", path); !canceled) {
    return std::unexpected(std::move(canceled.error()));
  }

  auto match = find_unique_text_match(*content, old_text, path, "old_text was not found", "old_text is not unique");
  if (!match) return std::unexpected(match.error());

  auto const original = *content;
  content->replace(match->position, match->size, new_text);
  auto diff = unified_diff(original, *content, path, path, kMaxPermissionDiffBytes);
  if (auto permission = ensure_permission(context, ava::permissions::Operation::EditFile, path, "", "",
                                          "tool requires permission", diff.text, diff.truncated);
      !permission) {
    return std::unexpected(permission.error());
  }
  if (auto canceled = check_canceled(context, "edit_file", path); !canceled) {
    return std::unexpected(std::move(canceled.error()));
  }
  auto written = write_file(context, path, *content,
                            WriteOptions{.permission_already_checked = true, .mutation_already_locked = true});
  if (!written) return std::unexpected(written.error());

  written->diff = std::move(diff.text);
  written->diff_truncated = diff.truncated;
  written->line_endings = to_string(match->content_analysis.line_endings);
  written->had_utf8_bom = match->content_analysis.has_utf8_bom;
  return written;
}

ava::core::VoidResult replace_file_with_staged_file(std::filesystem::path const& staged_path,
                                                    std::filesystem::path const& target_path)
{
  std::error_code rename_error;
  std::filesystem::rename(staged_path, target_path, rename_error);
  if (!rename_error) return {};

  auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to commit staged file write");
  error.with_context("path", target_path.string());
  error.with_context("temp_path", staged_path.string());
  error.with_context("cause", rename_error.message());
  return std::unexpected(std::move(error));
}

void remove_staged_file_best_effort(std::filesystem::path const& staged_path)
{
  std::error_code remove_error;
  std::filesystem::remove(staged_path, remove_error);
}

}  // namespace ava::tools
