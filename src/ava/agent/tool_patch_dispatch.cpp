#include "ava/agent/tool_patch_dispatch.h"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ava/agent/patch_staging.h"
#include "ava/agent/tool_arguments.h"
#include "ava/agent/tool_dispatch_support.h"
#include "ava/agent/tool_result_json.h"
#include "ava/core/json.h"
#include "ava/tools/diff_utils.h"
#include "ava/tools/edit_match.h"
#include "ava/tools/mutation_queue.h"

namespace ava::agent::detail {
namespace {

constexpr std::size_t kMaxMutationDiffBytes = 32 * 1024;

}  // namespace

ToolDispatchResult apply_patch_result(ava::tools::ToolContext const& context, ProviderToolCall const& call)
{
  auto const edits = ava::core::json::objects_in_array_field(call.arguments_json, "edits");
  if (edits.empty()) {
    return simple_error_result(call, ava::core::ErrorCategory::InvalidArgument,
                               "apply_patch requires a non-empty edits array");
  }
  if (edits.size() > 32) {
    return simple_error_result(call, ava::core::ErrorCategory::InvalidArgument,
                               "apply_patch supports at most 32 edits per call");
  }

  struct PatchEdit {
    std::filesystem::path target;
    std::string old_text;
    std::string new_text;
  };

  struct PatchReplacement {
    std::size_t position = 0;
    std::size_t old_size = 0;
    std::string new_text;
  };

  std::vector<PatchEdit> parsed_edits;
  parsed_edits.reserve(edits.size());
  std::map<std::filesystem::path, std::filesystem::path> permission_targets;
  for (std::size_t index = 0; index < edits.size(); ++index) {
    if (auto canceled = check_canceled(context, call); !canceled) return tool_error_result(call, canceled.error());
    auto path = required_safe_string_arg(edits[index], "path", call.name);
    if (!path) return tool_error_result(call, path.error());
    auto old_text = required_text_arg(edits[index], "old_text", call.name);
    if (!old_text) return tool_error_result(call, old_text.error());
    if (old_text->empty()) {
      auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "patch old_text must not be empty");
      error.with_context("path", *path);
      return tool_error_result(call, error);
    }
    auto new_text = required_text_arg(edits[index], "new_text", call.name);
    if (!new_text) return tool_error_result(call, new_text.error());

    auto const target = permission_dedupe_path(workspace_path(context, *path));
    permission_targets.emplace(target, target);
    parsed_edits.push_back(
        PatchEdit{.target = target, .old_text = std::move(*old_text), .new_text = std::move(*new_text)});
  }

  if (auto canceled = check_canceled(context, call); !canceled) return tool_error_result(call, canceled.error());
  for (auto const& [dedupe_path, target] : permission_targets) {
    static_cast<void>(dedupe_path);
    if (auto permission = ava::tools::ensure_permission(context, ava::permissions::Operation::ReadFile, target, "",
                                                        call.name, "patch read requires permission");
        !permission) {
      return tool_error_result(call, permission.error());
    }
  }

  std::vector<std::filesystem::path> lock_paths;
  lock_paths.reserve(permission_targets.size());
  for (auto const& [dedupe_path, target] : permission_targets) {
    static_cast<void>(dedupe_path);
    lock_paths.push_back(target);
  }
  auto patch_queue = context.mutation_queue ? context.mutation_queue : ava::tools::default_mutation_queue();
  [[maybe_unused]] auto mutation_lock = patch_queue->lock_paths(lock_paths);

  if (auto canceled = check_canceled(context, call); !canceled) return tool_error_result(call, canceled.error());
  std::map<std::filesystem::path, std::string> original_contents;
  std::map<std::filesystem::path, std::vector<PatchReplacement>> replacements_by_path;
  std::vector<std::filesystem::path> applied_paths;
  for (auto const& edit : parsed_edits) {
    if (auto canceled = check_canceled(context, call); !canceled) return tool_error_result(call, canceled.error());
    auto const& target = edit.target;
    if (!original_contents.contains(target)) {
      auto content = ava::tools::read_file(
          context, target, ava::tools::ReadOptions{.max_bytes = 10 * 1024 * 1024, .permission_already_checked = true});
      if (!content) return tool_error_result(call, content.error());
      if (content->truncated) {
        auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "file is too large for apply_patch");
        error.with_context("path", target.string());
        error.with_context("max_bytes", std::to_string(10 * 1024 * 1024));
        return tool_error_result(call, error);
      }
      original_contents.emplace(target, std::move(content->content));
      applied_paths.push_back(target);
    }

    auto const& content = original_contents[target];
    auto match = ava::tools::find_unique_text_match(content, edit.old_text, target, "patch old_text was not found",
                                                    "patch old_text is not unique");
    if (!match) return tool_error_result(call, match.error());
    replacements_by_path[target].push_back(
        PatchReplacement{.position = match->position, .old_size = match->size, .new_text = edit.new_text});
  }

  std::map<std::filesystem::path, std::string> final_contents;
  for (auto const& target : applied_paths) {
    auto content = original_contents[target];
    auto& replacements = replacements_by_path[target];
    std::ranges::sort(replacements, {}, [](PatchReplacement const& replacement) { return replacement.position; });
    for (std::size_t index = 1; index < replacements.size(); ++index) {
      auto const previous_end = replacements[index - 1].position + replacements[index - 1].old_size;
      if (replacements[index].position < previous_end) {
        auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "patch edits overlap");
        error.with_context("path", target.string());
        return tool_error_result(call, error);
      }
    }
    for (auto replacement = replacements.rbegin(); replacement != replacements.rend(); ++replacement) {
      content.replace(replacement->position, replacement->old_size, replacement->new_text);
    }
    final_contents.emplace(target, std::move(content));
  }

  std::string diff_text;
  bool diff_truncated = false;
  std::map<std::filesystem::path, ava::tools::DiffPreview> permission_diffs;
  for (auto const& target : applied_paths) {
    permission_diffs.emplace(target, ava::tools::unified_diff(original_contents[target], final_contents[target], target,
                                                              target, kMaxMutationDiffBytes));
  }
  for (auto const& target : applied_paths) {
    if (diff_text.size() >= kMaxMutationDiffBytes) {
      diff_truncated = true;
      break;
    }
    auto diff = ava::tools::unified_diff(original_contents[target], final_contents[target], target, target,
                                         kMaxMutationDiffBytes - diff_text.size());
    diff_text += std::move(diff.text);
    diff_truncated = diff_truncated || diff.truncated;
  }

  for (auto const& target : applied_paths) {
    if (auto canceled = check_canceled(context, call); !canceled) return tool_error_result(call, canceled.error());
    auto const diff = permission_diffs.find(target);
    auto const diff_preview = diff == permission_diffs.end() ? std::string_view{} : std::string_view(diff->second.text);
    auto const permission_diff_truncated = diff != permission_diffs.end() && diff->second.truncated;
    if (auto permission =
            ava::tools::ensure_permission(context, ava::permissions::Operation::EditFile, target, "", call.name,
                                          "patch edit requires permission", diff_preview, permission_diff_truncated);
        !permission) {
      return tool_error_result(call, permission.error());
    }
  }

  if (auto canceled = check_canceled(context, call); !canceled) return tool_error_result(call, canceled.error());
  auto staged = stage_patch_writes(context, applied_paths, final_contents);
  if (!staged) return tool_error_result(call, staged.error());
  if (auto canceled = check_canceled(context, call); !canceled) {
    cleanup_staged_patch_writes(*staged);
    return tool_error_result(call, canceled.error());
  }
  if (auto committed = commit_staged_patch_writes(*staged); !committed)
    return tool_error_result(call, committed.error());

  std::string text = "{\"tool\":\"apply_patch\",\"ok\":true,\"edits\":[";
  for (std::size_t index = 0; index < staged->size(); ++index) {
    auto const& write = (*staged)[index];

    if (index > 0) text += ',';
    text += "{\"path\":\"" + ava::core::json::escape(write.target.generic_string()) +
            "\",\"bytes_written\":" + std::to_string(write.bytes_written) + "}";
  }
  text += "]";
  append_changed_files_json(text, applied_paths);
  append_diff_json(text, diff_text, diff_truncated);
  text += "}";
  return ToolDispatchResult{.call_id = call.id, .name = call.name, .success = true, .result_text = std::move(text)};
}

}  // namespace ava::agent::detail
