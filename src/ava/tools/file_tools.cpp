#include "ava/tools/file_tools.h"

#include <optional>
#include <string_view>
#include <utility>

#include "ava/tools/diff_utils.h"
#include "ava/tools/edit_match.h"
#include "ava/tools/file_tools_support.h"

namespace ava::tools {

ava::core::Result<TextOutput> read_file(ToolContext const& context, std::filesystem::path const& path,
                                        ReadOptions options)
{
  if (auto canceled = detail::check_canceled(context, "read_file", path); !canceled) {
    return std::unexpected(std::move(canceled.error()));
  }
  if (!options.permission_already_checked) {
    if (auto permission =
            ensure_permission(context, ava::permissions::Operation::ReadFile, path, "", "", "tool requires permission");
        !permission) {
      return std::unexpected(permission.error());
    }
  }
  if (auto canceled = detail::check_canceled(context, "read_file", path); !canceled) {
    return std::unexpected(std::move(canceled.error()));
  }

  return detail::read_head_text(context, path, options.max_bytes);
}

ava::core::Result<FileMutationResult> write_file(ToolContext const& context, std::filesystem::path const& path,
                                                 std::string_view content, WriteOptions options)
{
  if (auto canceled = detail::check_canceled(context, "write_file", path); !canceled) {
    return std::unexpected(std::move(canceled.error()));
  }
  std::optional<detail::PermissionDiffPreview> preview;
  if (!options.permission_already_checked) {
    auto preview_result = detail::write_permission_diff_preview(context, path, content);
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
  if (auto canceled = detail::check_canceled(context, "write_file", path); !canceled) {
    return std::unexpected(std::move(canceled.error()));
  }

  if (options.mutation_already_locked) return detail::write_file_unlocked(context, path, content);
  [[maybe_unused]] auto mutation_lock = detail::effective_mutation_queue(context)->lock_path(path);
  return detail::write_file_unlocked(context, path, content);
}

ava::core::Result<FileMutationResult> edit_file(ToolContext const& context, std::filesystem::path const& path,
                                                std::string_view old_text, std::string_view new_text)
{
  if (old_text.empty()) {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "old_text must not be empty");
    error.with_context("path", path.string());
    return std::unexpected(std::move(error));
  }
  if (auto canceled = detail::check_canceled(context, "edit_file", path); !canceled) {
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
  if (auto canceled = detail::check_canceled(context, "edit_file", path); !canceled) {
    return std::unexpected(std::move(canceled.error()));
  }

  [[maybe_unused]] auto mutation_lock = detail::effective_mutation_queue(context)->lock_path(path);
  auto content = detail::read_all_text(context, path, "edit_file");
  if (!content) {
    return std::unexpected(content.error());
  }
  if (auto canceled = detail::check_canceled(context, "edit_file", path); !canceled) {
    return std::unexpected(std::move(canceled.error()));
  }

  auto match = find_unique_text_match(*content, old_text, path, "old_text was not found", "old_text is not unique");
  if (!match) return std::unexpected(match.error());

  auto const original = *content;
  content->replace(match->position, match->size, new_text);
  auto diff = unified_diff(original, *content, path, path, detail::kMaxPermissionDiffBytes);
  if (auto permission = ensure_permission(context, ava::permissions::Operation::EditFile, path, "", "",
                                          "tool requires permission", diff.text, diff.truncated);
      !permission) {
    return std::unexpected(permission.error());
  }
  if (auto canceled = detail::check_canceled(context, "edit_file", path); !canceled) {
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

}  // namespace ava::tools
