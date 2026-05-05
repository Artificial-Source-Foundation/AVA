#include "ava/agent/tool_dispatcher.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "ava/agent/patch_staging.h"
#include "ava/agent/question.h"
#include "ava/agent/question_answer_validation.h"
#include "ava/agent/tool_arguments.h"
#include "ava/agent/tool_dispatch_support.h"
#include "ava/agent/tool_file_dispatch.h"
#include "ava/agent/tool_lsp_dispatch.h"
#include "ava/agent/tool_process_dispatch.h"
#include "ava/agent/tool_registry.h"
#include "ava/agent/tool_result.h"
#include "ava/agent/tool_result_json.h"
#include "ava/agent/tool_search_dispatch.h"
#include "ava/core/json.h"
#include "ava/mcp/tool_broker.h"
#include "ava/plugin/tool_broker.h"
#include "ava/tools/diff_utils.h"
#include "ava/tools/edit_match.h"
#include "ava/tools/mutation_queue.h"
#include "ava/tools/webfetch_tool.h"

namespace ava::agent {
namespace {

constexpr std::size_t kMaxMutationDiffBytes = 32 * 1024;

using detail::bash_result;
using detail::canceled_error;
using detail::check_canceled;
using detail::context_for_provider_tool;
using detail::edit_file_result;
using detail::glob_result;
using detail::grep_result;
using detail::is_canceled;
using detail::is_lsp_diagnostics_metadata;
using detail::lsp_diagnostics_result;
using detail::read_file_result;
using detail::simple_error_result;
using detail::tool_error_result;
using detail::write_file_result;

ToolDispatchResult webfetch_result(ava::tools::ToolContext const& context, ProviderToolCall const& call)
{
  auto url = required_safe_string_arg(call.arguments_json, "url", call.name);
  if (!url) return tool_error_result(call, url.error());
  auto const tool_context = context_for_provider_tool(context, call);
  auto result = ava::tools::webfetch(
      tool_context, *url,
      ava::tools::WebFetchOptions{
          .max_bytes = optional_size_arg(call.arguments_json, "max_bytes", 1024 * 1024, 5 * 1024 * 1024),
          .timeout_ms = static_cast<int>(optional_size_arg(call.arguments_json, "timeout_ms", 30000, 120000)),
      });
  if (!result) return tool_error_result(call, result.error());
  return ToolDispatchResult{
      .call_id = call.id,
      .name = call.name,
      .success = true,
      .result_text = "{\"tool\":\"webfetch\",\"ok\":true,\"url\":\"" + ava::core::json::escape(result->url) +
                     "\",\"status_code\":" + std::to_string(result->status_code) + ",\"content_type\":\"" +
                     ava::core::json::escape(result->content_type) + "\",\"content\":\"" +
                     ava::core::json::escape(result->content) + "\",\"truncated\":" +
                     json_bool_literal(result->truncated) + ",\"total_bytes\":" + std::to_string(result->total_bytes) +
                     ",\"output_bytes\":" + std::to_string(result->output_bytes) + "}"};
}

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

ToolDispatchResult question_result(ava::tools::ToolContext const& context, ProviderToolCall const& call)
{
  auto prompt = parse_question_prompt(call.arguments_json, call.name);
  if (!prompt) return tool_error_result(call, prompt.error());
  if (!context.question_resolver) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Tool, "question resolver is unavailable");
    error.with_context("tool", call.name);
    return tool_error_result(call, error);
  }
  auto answer = context.question_resolver(*prompt);
  if (!answer) return tool_error_result(call, answer.error());
  if (auto valid_answer = validate_question_answer(*answer, call.name); !valid_answer) {
    return tool_error_result(call, valid_answer.error());
  }
  return ToolDispatchResult{.call_id = call.id,
                            .name = call.name,
                            .success = true,
                            .result_text = serialize_question_answer_result(*prompt, *answer)};
}

ToolExecutor builtin_tool_executor(std::string_view name)
{
  if (name == "read_file") return read_file_result;
  if (name == "write_file") return write_file_result;
  if (name == "edit_file") return edit_file_result;
  if (name == "glob") return glob_result;
  if (name == "grep") return grep_result;
  if (name == "bash") return bash_result;
  if (name == "webfetch") return webfetch_result;
  if (name == "lsp_diagnostics") return lsp_diagnostics_result;
  if (name == "apply_patch") return apply_patch_result;
  if (name == "question") return question_result;
  return nullptr;
}

ToolRegistry build_tool_registry(ava::tools::ToolContext const& context)
{
  ToolRegistry registry;
  for (auto const& entry : builtin_tool_registry().entries()) {
    auto registered = registry.register_tool(entry);
    if (!registered) {
      std::cerr << "tool registry failed: " << registered.error().format() << '\n';
      std::abort();
    }
  }
  ava::plugin::register_enabled_plugin_tools(registry, context);
  ava::mcp::register_enabled_mcp_tools(registry, context);
  return registry;
}

}  // namespace

ToolRegistry const& builtin_tool_registry()
{
  static auto const registry = [] {
    ToolRegistry builtins;
    for (auto const& metadata : builtin_tool_metadata()) {
      auto registered =
          builtins.register_tool(RegisteredTool{.metadata = own_tool_metadata(metadata),
                                                .executor = builtin_tool_executor(metadata.name),
                                                .source = ToolSource::Builtin,
                                                .source_id = "builtin",
                                                .requires_lsp_diagnostics = is_lsp_diagnostics_metadata(metadata)});
      if (!registered) {
        std::cerr << "builtin tool registry failed: " << registered.error().format() << '\n';
        std::abort();
      }
    }
    return builtins;
  }();
  return registry;
}

ToolDispatcher::ToolDispatcher(ava::tools::ToolContext context)
{
  if (!context.mutation_queue) context.mutation_queue = std::make_shared<ava::tools::MutationQueue>();
  context_ = std::move(context);
  registry_ = build_tool_registry(context_);
}

ava::core::Result<ToolDispatchResult> ToolDispatcher::dispatch(ProviderToolCall const& call) const
{
  ProviderToolCall const normalized = detail::normalize_provider_tool_call(call);
  if (auto valid = detail::validate_provider_tool_call(normalized); !valid) {
    return std::unexpected(std::move(valid.error()));
  }
  auto const* tool = registry_.find(normalized.name);
  if (tool != nullptr) {
    if (is_canceled(context_))
      return with_tool_result_payload(tool_error_result(normalized, canceled_error(normalized)));
    return with_tool_result_payload(tool->executor(context_, normalized));
  }
  return with_tool_result_payload(simple_error_result(normalized, ava::core::ErrorCategory::Tool, "unknown tool"));
}

std::span<ToolMetadata const> ToolDispatcher::tool_metadata()
{
  return builtin_tool_metadata();
}

std::vector<ToolMetadata> ToolDispatcher::tool_metadata(ava::tools::ToolContext const& context)
{
  return build_tool_registry(context).metadata();
}

std::vector<std::string> ToolDispatcher::tool_schemas_json()
{
  return tool_schemas_json(ava::tools::ToolContext{});
}

std::vector<std::string> ToolDispatcher::tool_schemas_json(ava::tools::ToolContext const& context)
{
  return build_tool_registry(context).tool_schemas_json(context);
}

}  // namespace ava::agent
