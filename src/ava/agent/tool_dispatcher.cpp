#include "ava/agent/tool_dispatcher.h"

#include <algorithm>
#include <filesystem>
#include <map>
#include <string_view>
#include <utility>
#include <vector>

#include "ava/agent/question.h"
#include "ava/core/ids.h"
#include "ava/core/json.h"
#include "ava/tools/bash_tool.h"
#include "ava/tools/search_tools.h"

namespace ava::agent {
namespace {

constexpr std::size_t kMaxProviderToolCallIdBytes = 256;
constexpr std::size_t kMaxQuestionAnswerSelectedOptions = 64;
constexpr std::size_t kMaxQuestionAnswerStringBytes = 8192;

std::string json_bool(bool value) { return value ? "true" : "false"; }

ava::tools::ToolContext context_for_provider_tool(const ava::tools::ToolContext& context, std::string_view tool_name) {
  auto tool_context = context;
  tool_context.permission_tool_name = std::string(tool_name);
  return tool_context;
}

std::string error_json(std::string_view tool, const ava::core::Error& error) {
  return "{\"tool\":\"" + ava::core::json::escape(tool) + "\",\"ok\":false,\"error\":{\"category\":\"" +
         ava::core::json::escape(ava::core::to_string(error.category())) + "\",\"message\":\"" +
         ava::core::json::escape(error.message()) + "\",\"details\":\"" + ava::core::json::escape(error.format()) +
         "\"}}";
}

ToolDispatchResult tool_error_result(const ProviderToolCall& call, const ava::core::Error& error) {
  return ToolDispatchResult{
      .call_id = call.id, .name = call.name, .success = false, .result_text = error_json(call.name, error)};
}

ToolDispatchResult simple_error_result(const ProviderToolCall& call, ava::core::ErrorCategory category,
                                       std::string message) {
  const auto error = ava::core::Error(category, std::move(message));
  return tool_error_result(call, error);
}

ava::core::Result<std::string> required_string_arg(std::string_view arguments, std::string_view field,
                                                   std::string_view tool_name) {
  auto value = ava::core::json::string_field(arguments, field);
  if (value) return *value;
  auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "tool argument is required");
  error.with_context("tool", std::string(tool_name));
  error.with_context("argument", std::string(field));
  return std::unexpected(std::move(error));
}

ava::core::VoidResult reject_nul_arg(std::string_view value, std::string_view field, std::string_view tool_name) {
  if (value.find('\0') == std::string_view::npos) return {};
  auto error =
      ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "tool argument contains a forbidden NUL byte");
  error.with_context("tool", std::string(tool_name));
  error.with_context("argument", std::string(field));
  return std::unexpected(std::move(error));
}

ava::core::VoidResult reject_control_arg(std::string_view value, std::string_view field, std::string_view tool_name) {
  for (const char ch : value) {
    const auto byte = static_cast<unsigned char>(ch);
    if (byte < 0x20 || byte == 0x7F) {
      auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument,
                                    "tool argument contains a forbidden control byte");
      error.with_context("tool", std::string(tool_name));
      error.with_context("argument", std::string(field));
      return std::unexpected(std::move(error));
    }
  }
  return {};
}

ava::core::VoidResult reject_control_value(std::string_view value, std::string_view field, std::string_view message) {
  for (const char ch : value) {
    const auto byte = static_cast<unsigned char>(ch);
    if (byte < 0x20 || byte == 0x7F) {
      auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, std::string(message));
      error.with_context("field", std::string(field));
      return std::unexpected(std::move(error));
    }
  }
  return {};
}

ava::core::Error question_answer_error(std::string_view tool_name, std::string_view field, std::string message) {
  auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, std::move(message));
  error.with_context("tool", std::string(tool_name));
  error.with_context("field", std::string(field));
  return error;
}

ava::core::VoidResult validate_question_answer(const QuestionAnswer& answer, std::string_view tool_name) {
  if (answer.selected_options.size() > kMaxQuestionAnswerSelectedOptions) {
    auto error = question_answer_error(tool_name, "selected_options", "question answer has too many selected options");
    error.with_context("max_options", std::to_string(kMaxQuestionAnswerSelectedOptions));
    return std::unexpected(std::move(error));
  }
  for (std::size_t index = 0; index < answer.selected_options.size(); ++index) {
    if (answer.selected_options[index].size() > kMaxQuestionAnswerStringBytes) {
      auto error = question_answer_error(tool_name, "selected_options", "question answer selected option is too long");
      error.with_context("index", std::to_string(index));
      error.with_context("max_bytes", std::to_string(kMaxQuestionAnswerStringBytes));
      return std::unexpected(std::move(error));
    }
  }
  if (answer.custom_text.size() > kMaxQuestionAnswerStringBytes) {
    auto error = question_answer_error(tool_name, "custom_text", "question answer custom text is too long");
    error.with_context("max_bytes", std::to_string(kMaxQuestionAnswerStringBytes));
    return std::unexpected(std::move(error));
  }
  return {};
}

ava::core::Result<std::string> required_text_arg(std::string_view arguments, std::string_view field,
                                                 std::string_view tool_name) {
  auto value = required_string_arg(arguments, field, tool_name);
  if (!value) return std::unexpected(value.error());
  if (auto safe = reject_nul_arg(*value, field, tool_name); !safe) return std::unexpected(safe.error());
  return *value;
}

ava::core::Result<std::string> required_safe_string_arg(std::string_view arguments, std::string_view field,
                                                        std::string_view tool_name) {
  auto value = required_string_arg(arguments, field, tool_name);
  if (!value) return std::unexpected(value.error());
  if (auto safe = reject_control_arg(*value, field, tool_name); !safe) return std::unexpected(safe.error());
  return *value;
}

std::filesystem::path workspace_path(const ava::tools::ToolContext& context, std::string_view path) {
  const std::filesystem::path parsed(path);
  if (parsed.is_absolute()) return parsed;
  return context.workspace_dir / parsed;
}

std::filesystem::path permission_dedupe_path(const std::filesystem::path& path) {
  std::error_code error;
  const auto canonical = std::filesystem::weakly_canonical(path, error);
  if (!error) return canonical;
  return std::filesystem::absolute(path).lexically_normal();
}

std::size_t optional_size_arg(std::string_view arguments, std::string_view field, std::size_t fallback,
                              std::size_t maximum) {
  const auto value = ava::core::json::integer_field(arguments, field);
  if (!value || *value <= 0) return fallback;
  const auto converted = static_cast<unsigned long long>(*value);
  if (converted > maximum) return maximum;
  return static_cast<std::size_t>(converted);
}

std::filesystem::path unique_patch_temp_path(const std::filesystem::path& target) {
  const auto stem = target.filename().string() + ".ava-patch-";
  for (int attempt = 0; attempt < 8; ++attempt) {
    auto candidate = target.parent_path() / (stem + ava::core::make_id("tmp") + ".tmp");
    std::error_code exists_error;
    if (!std::filesystem::exists(candidate, exists_error) && !exists_error) return candidate;
  }
  return target.parent_path() / (stem + ava::core::make_id("tmp") + ".tmp");
}

struct StagedPatchWrite {
  std::filesystem::path target;
  std::filesystem::path temp;
  std::size_t bytes_written = 0;
};

void cleanup_staged_patch_writes(const std::vector<StagedPatchWrite>& writes, std::size_t start_index = 0) {
  for (std::size_t index = start_index; index < writes.size(); ++index) {
    ava::tools::remove_staged_file_best_effort(writes[index].temp);
  }
}

ava::core::Result<std::vector<StagedPatchWrite>> stage_patch_writes(
    const ava::tools::ToolContext& context, const std::vector<std::filesystem::path>& paths,
    const std::map<std::filesystem::path, std::string>& final_contents) {
  std::vector<StagedPatchWrite> staged;
  staged.reserve(paths.size());

  for (const auto& path : paths) {
    const auto temp = unique_patch_temp_path(path);
    const auto content = final_contents.find(path);
    if (content == final_contents.end()) {
      auto error = ava::core::Error(ava::core::ErrorCategory::Unknown, "missing staged patch content");
      error.with_context("path", path.string());
      cleanup_staged_patch_writes(staged);
      return std::unexpected(std::move(error));
    }

    auto written = ava::tools::write_file(context, temp, content->second,
                                          ava::tools::WriteOptions{.permission_already_checked = true});
    if (!written) {
      ava::tools::remove_staged_file_best_effort(temp);
      cleanup_staged_patch_writes(staged);
      auto error = written.error();
      error.with_context("stage", "temporary_patch_write");
      error.with_context("target_path", path.string());
      error.with_context("temp_path", temp.string());
      return std::unexpected(std::move(error));
    }

    staged.push_back(StagedPatchWrite{.target = path, .temp = temp, .bytes_written = written->bytes_written});
  }

  return staged;
}

ava::core::VoidResult commit_staged_patch_writes(const std::vector<StagedPatchWrite>& staged) {
  for (std::size_t index = 0; index < staged.size(); ++index) {
    const auto& write = staged[index];
    if (auto committed = ava::tools::replace_file_with_staged_file(write.temp, write.target); !committed) {
      cleanup_staged_patch_writes(staged, index);
      auto error = committed.error();
      error.with_context("stage", "commit_staged_patch_write");
      error.with_context("atomicity", "all edits are staged before commit, but cross-file rename commit is not atomic");
      return std::unexpected(std::move(error));
    }
  }
  return {};
}

ToolDispatchResult read_file_result(const ava::tools::ToolContext& context, const ProviderToolCall& call) {
  auto path = required_safe_string_arg(call.arguments_json, "path", call.name);
  if (!path) return tool_error_result(call, path.error());
  const auto tool_context = context_for_provider_tool(context, call.name);
  auto result = ava::tools::read_file(
      tool_context, workspace_path(context, *path),
      ava::tools::ReadOptions{.max_bytes = optional_size_arg(call.arguments_json, "max_bytes", 50 * 1024, 512 * 1024)});
  if (!result) return tool_error_result(call, result.error());
  return ToolDispatchResult{
      .call_id = call.id,
      .name = call.name,
      .success = true,
      .result_text = "{\"tool\":\"read_file\",\"ok\":true,\"path\":\"" + ava::core::json::escape(*path) +
                     "\",\"content\":\"" + ava::core::json::escape(result->content) + "\",\"truncated\":" +
                     json_bool(result->truncated) + ",\"total_bytes\":" + std::to_string(result->total_bytes) +
                     ",\"output_bytes\":" + std::to_string(result->output_bytes) + "}"};
}

ToolDispatchResult write_file_result(const ava::tools::ToolContext& context, const ProviderToolCall& call) {
  auto path = required_safe_string_arg(call.arguments_json, "path", call.name);
  if (!path) return tool_error_result(call, path.error());
  auto content = required_text_arg(call.arguments_json, "content", call.name);
  if (!content) return tool_error_result(call, content.error());
  const auto tool_context = context_for_provider_tool(context, call.name);
  auto result = ava::tools::write_file(tool_context, workspace_path(context, *path), *content);
  if (!result) return tool_error_result(call, result.error());
  return ToolDispatchResult{.call_id = call.id,
                            .name = call.name,
                            .success = true,
                            .result_text = "{\"tool\":\"write_file\",\"ok\":true,\"path\":\"" +
                                           ava::core::json::escape(result->path.generic_string()) +
                                           "\",\"bytes_written\":" + std::to_string(result->bytes_written) + "}"};
}

ToolDispatchResult edit_file_result(const ava::tools::ToolContext& context, const ProviderToolCall& call) {
  auto path = required_safe_string_arg(call.arguments_json, "path", call.name);
  if (!path) return tool_error_result(call, path.error());
  auto old_text = required_text_arg(call.arguments_json, "old_text", call.name);
  if (!old_text) return tool_error_result(call, old_text.error());
  auto new_text = required_text_arg(call.arguments_json, "new_text", call.name);
  if (!new_text) return tool_error_result(call, new_text.error());
  const auto tool_context = context_for_provider_tool(context, call.name);
  auto result = ava::tools::edit_file(tool_context, workspace_path(context, *path), *old_text, *new_text);
  if (!result) return tool_error_result(call, result.error());
  return ToolDispatchResult{.call_id = call.id,
                            .name = call.name,
                            .success = true,
                            .result_text = "{\"tool\":\"edit_file\",\"ok\":true,\"path\":\"" +
                                           ava::core::json::escape(result->path.generic_string()) +
                                           "\",\"bytes_written\":" + std::to_string(result->bytes_written) + "}"};
}

ToolDispatchResult glob_result(const ava::tools::ToolContext& context, const ProviderToolCall& call) {
  auto pattern = required_safe_string_arg(call.arguments_json, "pattern", call.name);
  if (!pattern) return tool_error_result(call, pattern.error());
  const auto tool_context = context_for_provider_tool(context, call.name);
  auto result = ava::tools::glob_files(
      tool_context, *pattern,
      ava::tools::GlobOptions{.max_results = optional_size_arg(call.arguments_json, "max_results", 2000, 10000)});
  if (!result) return tool_error_result(call, result.error());
  std::string text =
      "{\"tool\":\"glob\",\"ok\":true,\"pattern\":\"" + ava::core::json::escape(*pattern) + "\",\"paths\":[";
  for (std::size_t index = 0; index < result->paths.size(); ++index) {
    if (index > 0) text += ',';
    text += "\"" + ava::core::json::escape(result->paths[index].generic_string()) + "\"";
  }
  text += "],\"truncated\":" + json_bool(result->truncated) +
          ",\"total_matches\":" + std::to_string(result->total_matches) + "}";
  return ToolDispatchResult{.call_id = call.id, .name = call.name, .success = true, .result_text = text};
}

ToolDispatchResult grep_result(const ava::tools::ToolContext& context, const ProviderToolCall& call) {
  auto pattern = required_text_arg(call.arguments_json, "pattern", call.name);
  if (!pattern) return tool_error_result(call, pattern.error());
  const auto include_value = ava::core::json::string_field(call.arguments_json, "include");
  if (include_value) {
    if (auto safe = reject_control_arg(*include_value, "include", call.name); !safe) {
      return tool_error_result(call, safe.error());
    }
  }
  const auto include = include_value.value_or("**/*");
  const auto tool_context = context_for_provider_tool(context, call.name);
  auto result = ava::tools::grep_files(
      tool_context, *pattern, include,
      ava::tools::GrepOptions{.max_matches = optional_size_arg(call.arguments_json, "max_matches", 2000, 10000)});
  if (!result) return tool_error_result(call, result.error());
  std::string text = "{\"tool\":\"grep\",\"ok\":true,\"pattern\":\"" + ava::core::json::escape(*pattern) +
                     "\",\"include\":\"" + ava::core::json::escape(include) + "\",\"matches\":[";
  for (std::size_t index = 0; index < result->matches.size(); ++index) {
    const auto& match = result->matches[index];
    if (index > 0) text += ',';
    text += "{\"path\":\"" + ava::core::json::escape(match.path.generic_string()) +
            "\",\"line_number\":" + std::to_string(match.line_number) + ",\"line\":\"" +
            ava::core::json::escape(match.line) + "\",\"line_truncated\":" + json_bool(match.line_truncated) + "}";
  }
  text += "],\"truncated\":" + json_bool(result->truncated) +
          ",\"total_matches\":" + std::to_string(result->total_matches) + "}";
  return ToolDispatchResult{.call_id = call.id, .name = call.name, .success = true, .result_text = text};
}

ToolDispatchResult bash_result(const ava::tools::ToolContext& context, const ProviderToolCall& call) {
  auto command = required_safe_string_arg(call.arguments_json, "command", call.name);
  if (!command) return tool_error_result(call, command.error());
  const auto tool_context = context_for_provider_tool(context, call.name);
  auto result = ava::tools::run_bash(
      tool_context, *command,
      ava::tools::BashOptions{
          .timeout = std::chrono::milliseconds(optional_size_arg(call.arguments_json, "timeout_ms", 30000, 120000)),
          .max_bytes = optional_size_arg(call.arguments_json, "max_bytes", 50 * 1024, 512 * 1024)});
  if (!result) return tool_error_result(call, result.error());
  return ToolDispatchResult{
      .call_id = call.id,
      .name = call.name,
      .success = result->exit_code == 0 && !result->timed_out,
      .result_text =
          "{\"tool\":\"bash\",\"ok\":" + json_bool(result->exit_code == 0 && !result->timed_out) +
          ",\"exit_code\":" + std::to_string(result->exit_code) + ",\"timed_out\":" + json_bool(result->timed_out) +
          ",\"truncated\":" + json_bool(result->truncated) + ",\"total_bytes\":" + std::to_string(result->total_bytes) +
          ",\"output\":\"" + ava::core::json::escape(result->output) + "\"}"};
}

ToolDispatchResult apply_patch_result(const ava::tools::ToolContext& context, const ProviderToolCall& call) {
  const auto edits = ava::core::json::objects_in_array_field(call.arguments_json, "edits");
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

    const auto target = workspace_path(context, *path);
    permission_targets.emplace(permission_dedupe_path(target), target);
    parsed_edits.push_back(
        PatchEdit{.target = target, .old_text = std::move(*old_text), .new_text = std::move(*new_text)});
  }

  for (const auto& [dedupe_path, target] : permission_targets) {
    static_cast<void>(dedupe_path);
    if (auto permission = ava::tools::ensure_permission(context, ava::permissions::Operation::EditFile, target, "",
                                                        call.name, "patch edit requires permission");
        !permission) {
      return tool_error_result(call, permission.error());
    }
  }

  std::map<std::filesystem::path, std::string> original_contents;
  std::map<std::filesystem::path, std::vector<PatchReplacement>> replacements_by_path;
  std::vector<std::filesystem::path> applied_paths;
  for (const auto& edit : parsed_edits) {
    const auto& target = edit.target;
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

    const auto& content = original_contents[target];
    const auto first = content.find(edit.old_text);
    if (first == std::string::npos) {
      auto error = ava::core::Error(ava::core::ErrorCategory::NotFound, "patch old_text was not found");
      error.with_context("path", target.string());
      return tool_error_result(call, error);
    }
    if (content.find(edit.old_text, first + edit.old_text.size()) != std::string::npos) {
      auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "patch old_text is not unique");
      error.with_context("path", target.string());
      return tool_error_result(call, error);
    }
    replacements_by_path[target].push_back(
        PatchReplacement{.position = first, .old_size = edit.old_text.size(), .new_text = edit.new_text});
  }

  std::map<std::filesystem::path, std::string> final_contents;
  for (const auto& target : applied_paths) {
    auto content = original_contents[target];
    auto& replacements = replacements_by_path[target];
    std::ranges::sort(replacements, {}, [](const PatchReplacement& replacement) { return replacement.position; });
    for (std::size_t index = 1; index < replacements.size(); ++index) {
      const auto previous_end = replacements[index - 1].position + replacements[index - 1].old_size;
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

  std::string text = "{\"tool\":\"apply_patch\",\"ok\":true,\"edits\":[";
  auto staged = stage_patch_writes(context, applied_paths, final_contents);
  if (!staged) return tool_error_result(call, staged.error());
  if (auto committed = commit_staged_patch_writes(*staged); !committed)
    return tool_error_result(call, committed.error());

  for (std::size_t index = 0; index < staged->size(); ++index) {
    const auto& write = (*staged)[index];

    if (index > 0) text += ',';
    text += "{\"path\":\"" + ava::core::json::escape(write.target.generic_string()) +
            "\",\"bytes_written\":" + std::to_string(write.bytes_written) + "}";
  }
  text += "]}";
  return ToolDispatchResult{.call_id = call.id, .name = call.name, .success = true, .result_text = std::move(text)};
}

ToolDispatchResult question_result(const ava::tools::ToolContext& context, const ProviderToolCall& call) {
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

}  // namespace

ToolDispatcher::ToolDispatcher(ava::tools::ToolContext context) : context_(std::move(context)) {}

ava::core::Result<ToolDispatchResult> ToolDispatcher::dispatch(const ProviderToolCall& call) const {
  const auto arguments = call.arguments_json.empty() ? std::string("{}") : call.arguments_json;
  const ProviderToolCall normalized{.id = call.id, .name = call.name, .arguments_json = arguments};
  if (normalized.id.empty()) {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "tool call id is required");
    error.with_context("tool", normalized.name);
    return std::unexpected(std::move(error));
  }
  if (normalized.id.size() > kMaxProviderToolCallIdBytes) {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "tool call id is too long");
    error.with_context("tool", normalized.name);
    error.with_context("max_bytes", std::to_string(kMaxProviderToolCallIdBytes));
    return std::unexpected(std::move(error));
  }
  if (auto safe_id = reject_control_value(normalized.id, "id", "tool call id contains a forbidden control byte");
      !safe_id) {
    safe_id.error().with_context("tool", normalized.name);
    return std::unexpected(std::move(safe_id.error()));
  }
  if (normalized.name == "read_file") return read_file_result(context_, normalized);
  if (normalized.name == "write_file") return write_file_result(context_, normalized);
  if (normalized.name == "edit_file") return edit_file_result(context_, normalized);
  if (normalized.name == "glob") return glob_result(context_, normalized);
  if (normalized.name == "grep") return grep_result(context_, normalized);
  if (normalized.name == "bash") return bash_result(context_, normalized);
  if (normalized.name == "apply_patch") return apply_patch_result(context_, normalized);
  if (normalized.name == "question") return question_result(context_, normalized);
  return simple_error_result(normalized, ava::core::ErrorCategory::Tool, "unknown tool");
}

std::vector<std::string> ToolDispatcher::tool_schemas_json() {
  return {
      R"({"type":"function","name":"read_file","description":"Read a workspace file through AVA permission checks.","parameters":{"type":"object","properties":{"path":{"type":"string"},"max_bytes":{"type":"integer"}},"required":["path"]}})",
      R"({"type":"function","name":"write_file","description":"Write a workspace file through AVA permission checks. Denied for source files in plan mode.","parameters":{"type":"object","properties":{"path":{"type":"string"},"content":{"type":"string"}},"required":["path","content"]}})",
      R"({"type":"function","name":"edit_file","description":"Replace one unique text span in a workspace file through AVA permission checks.","parameters":{"type":"object","properties":{"path":{"type":"string"},"old_text":{"type":"string"},"new_text":{"type":"string"}},"required":["path","old_text","new_text"]}})",
      R"({"type":"function","name":"glob","description":"List readable workspace files matching a glob pattern.","parameters":{"type":"object","properties":{"pattern":{"type":"string"},"max_results":{"type":"integer"}},"required":["pattern"]}})",
      R"({"type":"function","name":"grep","description":"Search readable workspace files for literal text.","parameters":{"type":"object","properties":{"pattern":{"type":"string"},"include":{"type":"string"},"max_matches":{"type":"integer"}},"required":["pattern"]}})",
      R"({"type":"function","name":"bash","description":"Run a permissioned local command without shell metacharacters.","parameters":{"type":"object","properties":{"command":{"type":"string"},"timeout_ms":{"type":"integer"},"max_bytes":{"type":"integer"}},"required":["command"]}})",
      R"({"type":"function","name":"apply_patch","description":"Apply up to 32 exact text replacements across workspace files. Each old_text must exist exactly once.","parameters":{"type":"object","properties":{"edits":{"type":"array","items":{"type":"object","properties":{"path":{"type":"string"},"old_text":{"type":"string"},"new_text":{"type":"string"}},"required":["path","old_text","new_text"]}}},"required":["edits"]}})",
      R"({"type":"function","name":"question","description":"Ask the user a clarification question through AVA's backend question resolver.","parameters":{"type":"object","properties":{"header":{"type":"string"},"question":{"type":"string"},"options":{"type":"array","items":{"oneOf":[{"type":"string"},{"type":"object","properties":{"value":{"type":"string"},"label":{"type":"string"}}}]}},"multiple":{"type":"boolean"},"allow_multiple":{"type":"boolean"},"custom":{"type":"boolean"},"allow_custom":{"type":"boolean"}},"required":["question"]}})"};
}

}  // namespace ava::agent
