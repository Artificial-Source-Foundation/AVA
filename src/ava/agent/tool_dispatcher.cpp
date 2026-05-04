#include "ava/agent/tool_dispatcher.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "ava/agent/question.h"
#include "ava/agent/tool_registry.h"
#include "ava/agent/tool_result.h"
#include "ava/core/ids.h"
#include "ava/core/json.h"
#include "ava/mcp/tool_broker.h"
#include "ava/plugin/tool_broker.h"
#include "ava/tools/bash_tool.h"
#include "ava/tools/diff_utils.h"
#include "ava/tools/edit_match.h"
#include "ava/tools/lsp_tools.h"
#include "ava/tools/mutation_queue.h"
#include "ava/tools/search_tools.h"
#include "ava/tools/webfetch_tool.h"

namespace ava::agent {
namespace {

constexpr std::size_t kMaxProviderToolCallIdBytes = 256;
constexpr std::size_t kMaxQuestionAnswerSelectedOptions = 64;
constexpr std::size_t kMaxQuestionAnswerStringBytes = 8192;
constexpr std::size_t kMaxMutationDiffBytes = 32 * 1024;
constexpr std::size_t kMaxLspProviderDiagnostics = 200;
constexpr std::size_t kMaxLspProviderJsonBytes = 64 * 1024;
constexpr std::size_t kMaxLspProviderPathBytes = 4096;

std::string json_bool(bool value)
{
  return value ? "true" : "false";
}

ava::tools::ToolContext context_for_provider_tool(ava::tools::ToolContext const& context, ProviderToolCall const& call)
{
  auto tool_context = context;
  tool_context.permission_tool_name = call.name;
  tool_context.current_tool_name = call.name;
  tool_context.current_call_id = call.id;
  return tool_context;
}

void append_spill_fields(std::string& text, std::filesystem::path const& path, bool spill_truncated)
{
  if (path.empty()) return;
  text += ",\"spill_file\":\"" + ava::core::json::escape(path.filename().generic_string()) + "\"";
  text += ",\"spill_truncated\":" + json_bool(spill_truncated);
}

std::string error_json(std::string_view tool, ava::core::Error const& error)
{
  return "{\"tool\":\"" + ava::core::json::escape(tool) + "\",\"ok\":false,\"error\":{\"category\":\"" +
         ava::core::json::escape(ava::core::to_string(error.category())) + "\",\"message\":\"" +
         ava::core::json::escape(error.message()) + "\",\"details\":\"" + ava::core::json::escape(error.format()) +
         "\"}}";
}

bool is_lsp_diagnostics_metadata(ToolMetadata const& tool)
{
  return tool.name == std::string_view("lsp_diagnostics");
}

ToolDispatchResult tool_error_result(ProviderToolCall const& call, ava::core::Error const& error)
{
  return ToolDispatchResult{.call_id = call.id,
                            .name = call.name,
                            .success = false,
                            .result_text = error_json(call.name, error),
                            .payload = [&] {
                              ava::agent::ToolResultPayload payload;
                              if (error.message().find("canceled") != std::string::npos ||
                                  error.message().find("cancelled") != std::string::npos) {
                                payload.status = ava::agent::ToolResultStatus::Canceled;
                              }
                              return payload;
                            }()};
}

ToolDispatchResult lsp_error_result(ProviderToolCall const& call, ava::core::Error const& error)
{
  if (error.message().find("canceled") != std::string::npos || error.message().find("cancelled") != std::string::npos) {
    return tool_error_result(call, error);
  }
  if (error.category() == ava::core::ErrorCategory::PermissionDenied ||
      error.category() == ava::core::ErrorCategory::InvalidArgument) {
    return tool_error_result(call, error);
  }
  auto redacted = ava::core::Error(error.category(), "LSP diagnostics failed");
  redacted.with_context("tool", call.name);
  return tool_error_result(call, redacted);
}

bool is_canceled(ava::tools::ToolContext const& context)
{
  return context.cancel_requested && context.cancel_requested();
}

ava::core::Error canceled_error(ProviderToolCall const& call)
{
  auto error = ava::core::Error(ava::core::ErrorCategory::Unknown, "tool canceled");
  error.with_context("tool", call.name);
  error.with_context("call_id", call.id);
  return error;
}

ava::core::VoidResult check_canceled(ava::tools::ToolContext const& context, ProviderToolCall const& call)
{
  if (!is_canceled(context)) return {};
  return std::unexpected(canceled_error(call));
}

ToolDispatchResult simple_error_result(ProviderToolCall const& call, ava::core::ErrorCategory category,
                                       std::string message)
{
  auto const error = ava::core::Error(category, std::move(message));
  return tool_error_result(call, error);
}

ava::core::Result<std::string> required_string_arg(std::string_view arguments, std::string_view field,
                                                   std::string_view tool_name)
{
  auto value = ava::core::json::string_field(arguments, field);
  if (value) return *value;
  auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "tool argument is required");
  error.with_context("tool", std::string(tool_name));
  error.with_context("argument", std::string(field));
  return std::unexpected(std::move(error));
}

ava::core::VoidResult reject_nul_arg(std::string_view value, std::string_view field, std::string_view tool_name)
{
  if (value.find('\0') == std::string_view::npos) return {};
  auto error =
      ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "tool argument contains a forbidden NUL byte");
  error.with_context("tool", std::string(tool_name));
  error.with_context("argument", std::string(field));
  return std::unexpected(std::move(error));
}

ava::core::VoidResult reject_control_arg(std::string_view value, std::string_view field, std::string_view tool_name)
{
  for (char const ch : value) {
    auto const byte = static_cast<unsigned char>(ch);
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

ava::core::VoidResult reject_control_value(std::string_view value, std::string_view field, std::string_view message)
{
  for (char const ch : value) {
    auto const byte = static_cast<unsigned char>(ch);
    if (byte < 0x20 || byte == 0x7F) {
      auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, std::string(message));
      error.with_context("field", std::string(field));
      return std::unexpected(std::move(error));
    }
  }
  return {};
}

ava::core::Error question_answer_error(std::string_view tool_name, std::string_view field, std::string message)
{
  auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, std::move(message));
  error.with_context("tool", std::string(tool_name));
  error.with_context("field", std::string(field));
  return error;
}

ava::core::VoidResult validate_question_answer(QuestionAnswer const& answer, std::string_view tool_name)
{
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
                                                 std::string_view tool_name)
{
  auto value = required_string_arg(arguments, field, tool_name);
  if (!value) return std::unexpected(value.error());
  if (auto safe = reject_nul_arg(*value, field, tool_name); !safe) return std::unexpected(safe.error());
  return *value;
}

ava::core::Result<std::string> required_safe_string_arg(std::string_view arguments, std::string_view field,
                                                        std::string_view tool_name)
{
  auto value = required_string_arg(arguments, field, tool_name);
  if (!value) return std::unexpected(value.error());
  if (auto safe = reject_control_arg(*value, field, tool_name); !safe) return std::unexpected(safe.error());
  return *value;
}

std::filesystem::path workspace_path(ava::tools::ToolContext const& context, std::string_view path)
{
  std::filesystem::path const parsed(path);
  if (parsed.is_absolute()) return parsed;
  return context.workspace_dir / parsed;
}

std::filesystem::path permission_dedupe_path(std::filesystem::path const& path)
{
  std::error_code error;
  auto const canonical = std::filesystem::weakly_canonical(path, error);
  if (!error) return canonical;
  return std::filesystem::absolute(path).lexically_normal();
}

std::size_t optional_size_arg(std::string_view arguments, std::string_view field, std::size_t fallback,
                              std::size_t maximum)
{
  auto const value = ava::core::json::integer_field(arguments, field);
  if (!value || *value <= 0) return fallback;
  auto const converted = static_cast<unsigned long long>(*value);
  if (converted > maximum) return maximum;
  return static_cast<std::size_t>(converted);
}

void append_changed_files_json(std::string& text, std::vector<std::filesystem::path> const& paths)
{
  text += ",\"changed_files\":[";
  for (std::size_t index = 0; index < paths.size(); ++index) {
    if (index > 0) text += ',';
    text += "\"" + ava::core::json::escape(paths[index].generic_string()) + "\"";
  }
  text += ']';
}

void append_diff_json(std::string& text, std::string_view diff, bool truncated)
{
  text += ",\"diff\":\"" + ava::core::json::escape(diff) + "\"";
  text += ",\"diff_truncated\":" + json_bool(truncated);
}

ava::core::Result<bool> optional_bool_arg(std::string_view arguments, std::string_view field, bool fallback,
                                          std::string_view tool_name)
{
  auto const start = ava::core::json::field_value_start(arguments, field);
  if (!start) return fallback;
  auto const is_value_boundary = [&arguments](std::size_t index) {
    return index >= arguments.size() || std::isspace(static_cast<unsigned char>(arguments[index])) != 0 ||
           arguments[index] == ',' || arguments[index] == '}' || arguments[index] == ']';
  };
  if (arguments.substr(*start, 4) == "true" && is_value_boundary(*start + 4)) return true;
  if (arguments.substr(*start, 5) == "false" && is_value_boundary(*start + 5)) return false;

  auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "tool argument must be a boolean");
  error.with_context("tool", std::string(tool_name));
  error.with_context("argument", std::string(field));
  return std::unexpected(std::move(error));
}

ava::core::VoidResult reject_provider_no_ignore(std::string_view arguments, std::string_view tool_name)
{
  auto no_ignore = optional_bool_arg(arguments, "no_ignore", false, tool_name);
  if (!no_ignore) return std::unexpected(no_ignore.error());
  if (!*no_ignore) return {};

  auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument,
                                "no_ignore requires explicit local control and is not available to provider tools");
  error.with_context("tool", std::string(tool_name));
  error.with_context("argument", "no_ignore");
  return std::unexpected(std::move(error));
}

std::filesystem::path unique_patch_temp_path(std::filesystem::path const& target)
{
  auto const stem = target.filename().string() + ".ava-patch-";
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

void cleanup_staged_patch_writes(std::vector<StagedPatchWrite> const& writes, std::size_t start_index = 0)
{
  for (std::size_t index = start_index; index < writes.size(); ++index) {
    ava::tools::remove_staged_file_best_effort(writes[index].temp);
  }
}

ava::core::VoidResult apply_existing_target_permissions_to_staged_file(std::filesystem::path const& target,
                                                                       std::filesystem::path const& temp)
{
  std::error_code status_error;
  auto const target_status = std::filesystem::status(target, status_error);
  if (status_error) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to read target permissions for patch write");
    error.with_context("path", target.string());
    error.with_context("temp_path", temp.string());
    error.with_context("cause", status_error.message());
    return std::unexpected(std::move(error));
  }
  if (!std::filesystem::exists(target_status)) return {};

  std::error_code permissions_error;
  std::filesystem::permissions(temp, target_status.permissions(), std::filesystem::perm_options::replace,
                               permissions_error);
  if (permissions_error) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to apply target permissions to patch write");
    error.with_context("path", target.string());
    error.with_context("temp_path", temp.string());
    error.with_context("cause", permissions_error.message());
    return std::unexpected(std::move(error));
  }
  return {};
}

ava::core::Result<std::vector<StagedPatchWrite>> stage_patch_writes(
    ava::tools::ToolContext const& context, std::vector<std::filesystem::path> const& paths,
    std::map<std::filesystem::path, std::string> const& final_contents)
{
  std::vector<StagedPatchWrite> staged;
  staged.reserve(paths.size());

  for (auto const& path : paths) {
    auto const temp = unique_patch_temp_path(path);
    auto const content = final_contents.find(path);
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

    if (auto permissions = apply_existing_target_permissions_to_staged_file(path, temp); !permissions) {
      ava::tools::remove_staged_file_best_effort(temp);
      cleanup_staged_patch_writes(staged);
      auto error = permissions.error();
      error.with_context("stage", "temporary_patch_permissions");
      return std::unexpected(std::move(error));
    }

    staged.push_back(StagedPatchWrite{.target = path, .temp = temp, .bytes_written = written->bytes_written});
  }

  return staged;
}

ava::core::VoidResult commit_staged_patch_writes(std::vector<StagedPatchWrite> const& staged)
{
  for (std::size_t index = 0; index < staged.size(); ++index) {
    auto const& write = staged[index];
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

ToolDispatchResult read_file_result(ava::tools::ToolContext const& context, ProviderToolCall const& call)
{
  auto path = required_safe_string_arg(call.arguments_json, "path", call.name);
  if (!path) return tool_error_result(call, path.error());
  auto const tool_context = context_for_provider_tool(context, call);
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

ToolDispatchResult write_file_result(ava::tools::ToolContext const& context, ProviderToolCall const& call)
{
  auto path = required_safe_string_arg(call.arguments_json, "path", call.name);
  if (!path) return tool_error_result(call, path.error());
  auto content = required_text_arg(call.arguments_json, "content", call.name);
  if (!content) return tool_error_result(call, content.error());
  auto const tool_context = context_for_provider_tool(context, call);
  auto result = ava::tools::write_file(tool_context, workspace_path(context, *path), *content);
  if (!result) return tool_error_result(call, result.error());
  return ToolDispatchResult{.call_id = call.id,
                            .name = call.name,
                            .success = true,
                            .result_text = "{\"tool\":\"write_file\",\"ok\":true,\"path\":\"" +
                                           ava::core::json::escape(result->path.generic_string()) +
                                           "\",\"bytes_written\":" + std::to_string(result->bytes_written) + "}"};
}

ToolDispatchResult edit_file_result(ava::tools::ToolContext const& context, ProviderToolCall const& call)
{
  auto path = required_safe_string_arg(call.arguments_json, "path", call.name);
  if (!path) return tool_error_result(call, path.error());
  auto old_text = required_text_arg(call.arguments_json, "old_text", call.name);
  if (!old_text) return tool_error_result(call, old_text.error());
  auto new_text = required_text_arg(call.arguments_json, "new_text", call.name);
  if (!new_text) return tool_error_result(call, new_text.error());
  auto const tool_context = context_for_provider_tool(context, call);
  auto result = ava::tools::edit_file(tool_context, workspace_path(context, *path), *old_text, *new_text);
  if (!result) return tool_error_result(call, result.error());
  return ToolDispatchResult{.call_id = call.id,
                            .name = call.name,
                            .success = true,
                            .result_text = "{\"tool\":\"edit_file\",\"ok\":true,\"path\":\"" +
                                           ava::core::json::escape(result->path.generic_string()) +
                                           "\",\"bytes_written\":" + std::to_string(result->bytes_written) +
                                           ",\"diff\":\"" + ava::core::json::escape(result->diff) +
                                           "\",\"diff_truncated\":" + json_bool(result->diff_truncated) +
                                           ",\"line_endings\":\"" + ava::core::json::escape(result->line_endings) +
                                           "\",\"utf8_bom\":" + json_bool(result->had_utf8_bom) + "}"};
}

ToolDispatchResult glob_result(ava::tools::ToolContext const& context, ProviderToolCall const& call)
{
  auto pattern = required_safe_string_arg(call.arguments_json, "pattern", call.name);
  if (!pattern) return tool_error_result(call, pattern.error());
  if (auto no_ignore_allowed = reject_provider_no_ignore(call.arguments_json, call.name); !no_ignore_allowed) {
    return tool_error_result(call, no_ignore_allowed.error());
  }
  auto const tool_context = context_for_provider_tool(context, call);
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
  text +=
      "],\"truncated\":" + json_bool(result->truncated) + ",\"total_matches\":" + std::to_string(result->total_matches);
  append_spill_fields(text, result->spill_path, result->spill_truncated);
  text += "}";
  return ToolDispatchResult{.call_id = call.id, .name = call.name, .success = true, .result_text = text};
}

ToolDispatchResult grep_result(ava::tools::ToolContext const& context, ProviderToolCall const& call)
{
  auto pattern = required_text_arg(call.arguments_json, "pattern", call.name);
  if (!pattern) return tool_error_result(call, pattern.error());
  auto const include_value = ava::core::json::string_field(call.arguments_json, "include");
  if (include_value) {
    if (auto safe = reject_control_arg(*include_value, "include", call.name); !safe) {
      return tool_error_result(call, safe.error());
    }
  }
  auto const include = include_value.value_or("**/*");
  if (auto no_ignore_allowed = reject_provider_no_ignore(call.arguments_json, call.name); !no_ignore_allowed) {
    return tool_error_result(call, no_ignore_allowed.error());
  }
  auto const tool_context = context_for_provider_tool(context, call);
  auto result = ava::tools::grep_files(
      tool_context, *pattern, include,
      ava::tools::GrepOptions{.max_matches = optional_size_arg(call.arguments_json, "max_matches", 2000, 10000)});
  if (!result) return tool_error_result(call, result.error());
  std::string text = "{\"tool\":\"grep\",\"ok\":true,\"pattern\":\"" + ava::core::json::escape(*pattern) +
                     "\",\"include\":\"" + ava::core::json::escape(include) + "\",\"matches\":[";
  for (std::size_t index = 0; index < result->matches.size(); ++index) {
    auto const& match = result->matches[index];
    if (index > 0) text += ',';
    text += "{\"path\":\"" + ava::core::json::escape(match.path.generic_string()) +
            "\",\"line_number\":" + std::to_string(match.line_number) + ",\"line\":\"" +
            ava::core::json::escape(match.line) + "\",\"line_truncated\":" + json_bool(match.line_truncated) + "}";
  }
  text +=
      "],\"truncated\":" + json_bool(result->truncated) + ",\"total_matches\":" + std::to_string(result->total_matches);
  append_spill_fields(text, result->spill_path, result->spill_truncated);
  text += "}";
  return ToolDispatchResult{.call_id = call.id, .name = call.name, .success = true, .result_text = text};
}

ToolDispatchResult bash_result(ava::tools::ToolContext const& context, ProviderToolCall const& call)
{
  auto command = required_safe_string_arg(call.arguments_json, "command", call.name);
  if (!command) return tool_error_result(call, command.error());
  auto const tool_context = context_for_provider_tool(context, call);
  auto result = ava::tools::run_bash(
      tool_context, *command,
      ava::tools::BashOptions{
          .timeout = std::chrono::milliseconds(optional_size_arg(call.arguments_json, "timeout_ms", 30000, 120000)),
          .max_bytes = optional_size_arg(call.arguments_json, "max_bytes", 50 * 1024, 512 * 1024)});
  if (!result) return tool_error_result(call, result.error());
  return ToolDispatchResult{.call_id = call.id,
                            .name = call.name,
                            .success = result->exit_code == 0 && !result->timed_out && !result->canceled,
                            .result_text =
                                [&] {
                                  std::string text =
                                      "{\"tool\":\"bash\",\"ok\":" +
                                      json_bool(result->exit_code == 0 && !result->timed_out && !result->canceled) +
                                      ",\"exit_code\":" + std::to_string(result->exit_code) +
                                      ",\"timed_out\":" + json_bool(result->timed_out) +
                                      ",\"canceled\":" + json_bool(result->canceled) +
                                      ",\"truncated\":" + json_bool(result->truncated) +
                                      ",\"total_bytes\":" + std::to_string(result->total_bytes) + ",\"output\":\"" +
                                      ava::core::json::escape(result->output) + "\"";
                                  append_spill_fields(text, result->spill_path, result->spill_truncated);
                                  text += "}";
                                  return text;
                                }(),
                            .payload =
                                [&] {
                                  ava::agent::ToolResultPayload payload;
                                  payload.status = result->canceled ? ava::agent::ToolResultStatus::Canceled
                                                                    : ava::agent::ToolResultStatus::Success;
                                  return payload;
                                }()};
}

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
                     ava::core::json::escape(result->content) + "\",\"truncated\":" + json_bool(result->truncated) +
                     ",\"total_bytes\":" + std::to_string(result->total_bytes) +
                     ",\"output_bytes\":" + std::to_string(result->output_bytes) + "}"};
}

ToolDispatchResult lsp_diagnostics_result(ava::tools::ToolContext const& context, ProviderToolCall const& call)
{
  auto path = required_safe_string_arg(call.arguments_json, "path", call.name);
  if (!path) return tool_error_result(call, path.error());
  if (path->size() > kMaxLspProviderPathBytes) {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "lsp_diagnostics path is too long");
    error.with_context("max_bytes", std::to_string(kMaxLspProviderPathBytes));
    return tool_error_result(call, error);
  }
  auto const tool_context = context_for_provider_tool(context, call);
  auto result = ava::tools::lsp_diagnostics(tool_context, workspace_path(context, *path));
  if (!result) return lsp_error_result(call, result.error());

  std::string text =
      "{\"tool\":\"lsp_diagnostics\",\"ok\":true,\"path\":\"" + ava::core::json::escape(*path) + "\",\"diagnostics\":[";
  auto const total_diagnostics = result->diagnostics.size();
  bool truncated = false;
  for (std::size_t index = 0; index < result->diagnostics.size(); ++index) {
    if (index >= kMaxLspProviderDiagnostics) {
      truncated = true;
      break;
    }
    auto const& diagnostic = result->diagnostics[index];
    auto entry = std::string{"{\"severity\":"} + std::to_string(diagnostic.severity) + ",\"message\":\"" +
                 ava::core::json::escape(diagnostic.message) + "\",\"line\":" + std::to_string(diagnostic.line) +
                 ",\"column\":" + std::to_string(diagnostic.column) + ",\"code\":\"" +
                 ava::core::json::escape(diagnostic.code) + "\"}";
    if (text.size() + entry.size() + 80 > kMaxLspProviderJsonBytes) {
      truncated = true;
      break;
    }
    if (index > 0) text += ',';
    text += std::move(entry);
  }
  text +=
      "],\"truncated\":" + json_bool(truncated) + ",\"total_diagnostics\":" + std::to_string(total_diagnostics) + "}";
  return ToolDispatchResult{.call_id = call.id, .name = call.name, .success = true, .result_text = std::move(text)};
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
  auto const arguments = call.arguments_json.empty() ? std::string("{}") : call.arguments_json;
  ProviderToolCall const normalized{.id = call.id, .name = call.name, .arguments_json = arguments};
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
