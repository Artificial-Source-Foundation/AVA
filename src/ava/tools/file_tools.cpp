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

std::string effective_tool_name(const ToolContext& context, ava::permissions::Operation operation,
                                std::string_view tool_name) {
  if (!tool_name.empty()) return std::string(tool_name);
  if (!context.permission_tool_name.empty()) return context.permission_tool_name;
  return ava::permissions::to_string(operation);
}

ava::core::VoidResult record_permission_audit(const ToolContext& context, const PermissionAuditEvent& event) {
  if (!context.permission_audit_sink) return {};
  return context.permission_audit_sink(event);
}

PermissionAuditEvent audit_event(const ToolContext& context, ava::permissions::Operation operation,
                                 std::string tool_name, const ava::permissions::PermissionDecision& decision,
                                 const std::filesystem::path& target_path, std::string_view command) {
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
                                         const ava::permissions::PermissionDecision& decision,
                                         const std::filesystem::path& target_path, std::string_view command,
                                         std::string_view resolution_context) {
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

ava::core::Result<std::string> read_all_text(const std::filesystem::path& path) {
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
    file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const auto count = file.gcount();
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
  if (!file.eof() && file.fail()) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed while reading file");
    error.with_context("path", path.string());
    return std::unexpected(std::move(error));
  }
  return content;
}

ava::core::Result<TextOutput> read_head_text(const std::filesystem::path& path, std::size_t max_bytes) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to open file for reading");
    error.with_context("path", path.string());
    return std::unexpected(std::move(error));
  }

  TextOutput output;
  std::array<char, 4096> buffer{};
  while (file) {
    file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const auto count = static_cast<std::size_t>(file.gcount());
    if (count == 0) {
      continue;
    }
    output.total_bytes += count;
    if (output.content.size() < max_bytes) {
      const auto remaining = max_bytes - output.content.size();
      output.content.append(buffer.data(), std::min(remaining, count));
    }
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

std::filesystem::path write_parent_path(const std::filesystem::path& path) {
  const auto parent = path.parent_path();
  if (!parent.empty()) return parent;
  return ".";
}

std::filesystem::path unique_write_temp_path(const std::filesystem::path& target) {
  const auto parent = write_parent_path(target);
  const auto filename = target.filename().empty() ? std::string("file") : target.filename().string();
  const auto stem = "." + filename + ".ava-write-";
  for (int attempt = 0; attempt < 8; ++attempt) {
    auto candidate = parent / (stem + ava::core::make_id("tmp") + ".tmp");
    std::error_code exists_error;
    if (!std::filesystem::exists(candidate, exists_error) && !exists_error) return candidate;
  }
  return parent / (stem + ava::core::make_id("tmp") + ".tmp");
}

ava::core::Error io_error(std::string message, const std::filesystem::path& path, std::string cause) {
  auto error = ava::core::Error(ava::core::ErrorCategory::Io, std::move(message));
  error.with_context("path", path.string());
  if (!cause.empty()) error.with_context("cause", std::move(cause));
  return error;
}

ava::core::Error staged_io_error(std::string message, const std::filesystem::path& target_path,
                                 const std::filesystem::path& temp_path, std::string cause) {
  auto error = ava::core::Error(ava::core::ErrorCategory::Io, std::move(message));
  error.with_context("path", target_path.string());
  error.with_context("temp_path", temp_path.string());
  if (!cause.empty()) error.with_context("cause", std::move(cause));
  return error;
}

std::string errno_cause(int value) {
  if (value == 0) return "stream operation failed";
  return std::generic_category().message(value);
}

std::shared_ptr<MutationQueue> effective_mutation_queue(const ToolContext& context) {
  if (context.mutation_queue) return context.mutation_queue;
  return default_mutation_queue();
}

std::optional<PermissionDiffPreview> write_permission_diff_preview(const ToolContext& context,
                                                                   const std::filesystem::path& path,
                                                                   std::string_view content) {
  std::error_code exists_error;
  const bool exists = std::filesystem::exists(path, exists_error);
  if (exists_error) return std::nullopt;

  std::string original;
  if (exists) {
    const auto read_decision = ava::permissions::decide(ava::permissions::PermissionRequest{
        .operation = ava::permissions::Operation::ReadFile,
        .mode = context.mode,
        .workspace_dir = context.workspace_dir,
        .target_path = path,
        .command = "",
    });
    if (read_decision.action != ava::permissions::PermissionAction::Allow) return std::nullopt;
    auto current = read_all_text(path);
    if (!current) return std::nullopt;
    original = std::move(*current);
  }

  auto diff = unified_diff(original, content, path, path, kMaxPermissionDiffBytes);
  return PermissionDiffPreview{.text = std::move(diff.text), .truncated = diff.truncated};
}

ava::core::Result<FileMutationResult> write_file_unlocked(const std::filesystem::path& path, std::string_view content) {
  const auto parent_path = path.parent_path();
  if (!parent_path.empty()) {
    std::error_code mkdir_error;
    std::filesystem::create_directories(parent_path, mkdir_error);
    if (mkdir_error) {
      return std::unexpected(io_error("failed to create parent directory", parent_path, mkdir_error.message()));
    }
  }

  std::error_code status_error;
  const auto existing_status = std::filesystem::status(path, status_error);
  const bool preserve_permissions = !status_error && std::filesystem::exists(existing_status);
  const auto existing_permissions = existing_status.permissions();

  const auto temp_path = unique_write_temp_path(path);
  errno = 0;
  std::ofstream file(temp_path, std::ios::binary | std::ios::trunc);
  if (!file) {
    return std::unexpected(
        staged_io_error("failed to open temporary file for writing", path, temp_path, errno_cause(errno)));
  }
  errno = 0;
  file << content;
  if (!file) {
    file.close();
    remove_staged_file_best_effort(temp_path);
    return std::unexpected(staged_io_error("failed to write temporary file", path, temp_path, errno_cause(errno)));
  }
  errno = 0;
  file.close();
  if (!file) {
    remove_staged_file_best_effort(temp_path);
    return std::unexpected(
        staged_io_error("failed to close temporary file after writing", path, temp_path, errno_cause(errno)));
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

ava::core::VoidResult ensure_permission(const ToolContext& context, ava::permissions::Operation operation,
                                        const std::filesystem::path& target_path, std::string_view command,
                                        std::string_view tool_name, std::string_view error_message,
                                        std::string_view diff_preview, bool diff_truncated) {
  const auto request_tool_name = effective_tool_name(context, operation, tool_name);
  const auto decision = ava::permissions::decide(ava::permissions::PermissionRequest{
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

  const auto resolution_context =
      resolution ? ava::permissions::to_string(*resolution) : std::string("resolver_failed");
  return std::unexpected(permission_denied_error(error_message, decision, target_path, command, resolution_context));
}

std::string permission_audit_data_json(const PermissionAuditEvent& event) {
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

ava::core::Result<TextOutput> read_file(const ToolContext& context, const std::filesystem::path& path,
                                        ReadOptions options) {
  if (!options.permission_already_checked) {
    if (auto permission =
            ensure_permission(context, ava::permissions::Operation::ReadFile, path, "", "", "tool requires permission");
        !permission) {
      return std::unexpected(permission.error());
    }
  }

  return read_head_text(path, options.max_bytes);
}

ava::core::Result<FileMutationResult> write_file(const ToolContext& context, const std::filesystem::path& path,
                                                 std::string_view content, WriteOptions options) {
  if (!options.permission_already_checked) {
    const auto preview = write_permission_diff_preview(context, path, content);
    const auto diff_preview = preview ? std::string_view(preview->text) : std::string_view{};
    if (auto permission =
            ensure_permission(context, ava::permissions::Operation::EditFile, path, "", "", "tool requires permission",
                              diff_preview, preview ? preview->truncated : false);
        !permission) {
      return std::unexpected(permission.error());
    }
  }

  if (options.mutation_already_locked) return write_file_unlocked(path, content);
  [[maybe_unused]] auto mutation_lock = effective_mutation_queue(context)->lock_path(path);
  return write_file_unlocked(path, content);
}

ava::core::Result<FileMutationResult> edit_file(const ToolContext& context, const std::filesystem::path& path,
                                                std::string_view old_text, std::string_view new_text) {
  if (old_text.empty()) {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "old_text must not be empty");
    error.with_context("path", path.string());
    return std::unexpected(std::move(error));
  }
  const auto read_decision = ava::permissions::decide(ava::permissions::PermissionRequest{
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
  const auto edit_decision = ava::permissions::decide(ava::permissions::PermissionRequest{
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

  [[maybe_unused]] auto mutation_lock = effective_mutation_queue(context)->lock_path(path);
  auto content = read_all_text(path);
  if (!content) {
    return std::unexpected(content.error());
  }

  auto match = find_unique_text_match(*content, old_text, path, "old_text was not found", "old_text is not unique");
  if (!match) return std::unexpected(match.error());

  const auto original = *content;
  content->replace(match->position, match->size, new_text);
  auto diff = unified_diff(original, *content, path, path, kMaxPermissionDiffBytes);
  if (auto permission = ensure_permission(context, ava::permissions::Operation::EditFile, path, "", "",
                                          "tool requires permission", diff.text, diff.truncated);
      !permission) {
    return std::unexpected(permission.error());
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

ava::core::VoidResult replace_file_with_staged_file(const std::filesystem::path& staged_path,
                                                    const std::filesystem::path& target_path) {
  std::error_code rename_error;
  std::filesystem::rename(staged_path, target_path, rename_error);
  if (!rename_error) return {};

  auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to commit staged file write");
  error.with_context("path", target_path.string());
  error.with_context("temp_path", staged_path.string());
  error.with_context("cause", rename_error.message());
  return std::unexpected(std::move(error));
}

void remove_staged_file_best_effort(const std::filesystem::path& staged_path) {
  std::error_code remove_error;
  std::filesystem::remove(staged_path, remove_error);
}

}  // namespace ava::tools
