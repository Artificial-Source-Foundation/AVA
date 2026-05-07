#include "ava/agent/tool_dispatcher.h"

#include "ava/agent/question.h"
#include "ava/agent/tool_dispatch_common.h"
#include "ava/agent/tool_dispatch_patch.h"
#include "ava/agent/tool_registry.h"
#include "ava/agent/tool_result.h"

#include "ava/tools/bash_tool.h"
#include "ava/tools/lsp_tools.h"
#include "ava/tools/mutation_queue.h"
#include "ava/tools/search_tools.h"
#include "ava/tools/webfetch_tool.h"
#include "ava/tools/websearch_tool.h"

#include "ava/plugin/tool_broker.h"

#include "ava/mcp/tool_broker.h"

#include "ava/context/skill_loader.h"

#include "ava/core/json.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

namespace ava::agent {
namespace {

constexpr std::size_t kMaxProviderToolCallIdBytes = 256;
constexpr std::size_t kMaxQuestionAnswerSelectedOptions = 64;
constexpr std::size_t kMaxQuestionAnswerStringBytes = 8192;
constexpr std::size_t kMaxLspProviderDiagnostics = 200;
constexpr std::size_t kMaxLspProviderJsonBytes = 64 * 1024;
constexpr std::size_t kMaxLspProviderPathBytes = 4096;

using namespace ava::agent::tool_dispatch;

bool is_lsp_diagnostics_metadata(ToolMetadata const& tool)
{
  return tool.name == std::string_view("lsp_diagnostics");
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

ToolDispatchResult read_file_result(ava::tools::ToolContext const& context, ProviderToolCall const& call)
{
  auto path = required_safe_string_arg(call.arguments_json, "path", call.name);
  if (!path) return tool_error_result(call, path.error());
  auto const tool_context = context_for_provider_tool(context, call);
  auto result = ava::tools::read_file(
      tool_context, workspace_path(context, *path),
      ava::tools::ReadOptions{.max_bytes = optional_size_arg(call.arguments_json, "max_bytes", 50 * 1024, 512 * 1024),
                              .offset_line = optional_size_arg(call.arguments_json, "offset", 1, 100000000),
                              .max_lines = optional_size_arg(call.arguments_json, "limit", 200, 100000)});
  if (!result) return tool_error_result(call, result.error());
  std::string text =
      "{\"tool\":\"read_file\",\"ok\":true,\"path\":\"" + ava::core::json::escape(*path) + "\",\"content\":\"" +
      ava::core::json::escape(result->content) + "\",\"truncated\":" + json_bool(result->truncated) +
      ",\"byte_limited\":" + json_bool(result->byte_limited) + ",\"line_limited\":" + json_bool(result->line_limited) +
      ",\"total_bytes\":" + std::to_string(result->total_bytes) +
      ",\"output_bytes\":" + std::to_string(result->output_bytes) +
      ",\"output_lines\":" + std::to_string(result->output_lines) +
      ",\"start_line\":" + std::to_string(result->start_line) + ",\"end_line\":" + std::to_string(result->end_line) +
      ",\"total_lines\":" + std::to_string(result->total_lines);
  if (result->next_offset_line > 0) {
    text += ",\"next_offset\":" + std::to_string(result->next_offset_line);
    text += ",\"next_offset_line\":" + std::to_string(result->next_offset_line);
    text += ",\"truncation_hint\":\"Call read_file again with offset=" + std::to_string(result->next_offset_line) +
            " to continue.\"";
  } else if (result->byte_limited) {
    text += ",\"truncation_hint\":\"Increase max_bytes to read more of this range.\"";
  }
  text += "}";
  return ToolDispatchResult{.call_id = call.id, .name = call.name, .success = true, .result_text = std::move(text)};
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

ToolDispatchResult list_directory_result(ava::tools::ToolContext const& context, ProviderToolCall const& call)
{
  auto path_value = ava::core::json::string_field(call.arguments_json, "path");
  if (path_value) {
    if (auto safe = reject_control_arg(*path_value, "path", call.name); !safe) {
      return tool_error_result(call, safe.error());
    }
  }
  auto const path = path_value.value_or(".");
  auto const tool_context = context_for_provider_tool(context, call);
  auto result =
      ava::tools::list_directory(tool_context, workspace_path(context, path),
                                 ava::tools::ListDirectoryOptions{
                                     .max_entries = optional_size_arg(call.arguments_json, "max_entries", 500, 5000)});
  if (!result) return tool_error_result(call, result.error());
  std::string text =
      "{\"tool\":\"list_directory\",\"ok\":true,\"path\":\"" + ava::core::json::escape(path) + "\",\"entries\":[";
  for (std::size_t index = 0; index < result->entries.size(); ++index) {
    auto const& entry = result->entries[index];
    if (index > 0) text += ',';
    text += "{\"name\":\"" + ava::core::json::escape(entry.name) + "\",\"type\":\"" +
            std::string(entry.directory ? "directory" : "file") + "\",\"size\":" + std::to_string(entry.size) + "}";
  }
  text += "],\"truncated\":" + json_bool(result->truncated) +
          ",\"total_entries\":" + std::to_string(result->total_entries) + "}";
  return ToolDispatchResult{.call_id = call.id, .name = call.name, .success = true, .result_text = std::move(text)};
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
  auto literal = optional_bool_arg(call.arguments_json, "literal", true, call.name);
  if (!literal) return tool_error_result(call, literal.error());
  auto case_insensitive = optional_bool_arg(call.arguments_json, "case_insensitive", false, call.name);
  if (!case_insensitive) return tool_error_result(call, case_insensitive.error());
  auto const tool_context = context_for_provider_tool(context, call);
  auto result = ava::tools::grep_files(
      tool_context, *pattern, include,
      ava::tools::GrepOptions{.max_matches = optional_size_arg(call.arguments_json, "max_matches", 2000, 10000),
                              .literal = *literal,
                              .case_insensitive = *case_insensitive});
  if (!result) return tool_error_result(call, result.error());
  std::string text = "{\"tool\":\"grep\",\"ok\":true,\"pattern\":\"" + ava::core::json::escape(*pattern) +
                     "\",\"include\":\"" + ava::core::json::escape(include) + "\",\"literal\":" + json_bool(*literal) +
                     ",\"case_insensitive\":" + json_bool(*case_insensitive) + ",\"matches\":[";
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
  auto max_lines = optional_size_arg(call.arguments_json, "max_lines", 0, 100000);
  if (max_lines == 0) max_lines = optional_size_arg(call.arguments_json, "limit", 200, 100000);
  auto const tool_context = context_for_provider_tool(context, call);
  auto result = ava::tools::run_bash(
      tool_context, *command,
      ava::tools::BashOptions{
          .timeout = std::chrono::milliseconds(optional_size_arg(call.arguments_json, "timeout_ms", 30000, 120000)),
          .max_bytes = optional_size_arg(call.arguments_json, "max_bytes", 50 * 1024, 512 * 1024),
          .max_lines = max_lines});
  if (!result) return tool_error_result(call, result.error());
  return ToolDispatchResult{
      .call_id = call.id,
      .name = call.name,
      .success = result->exit_code == 0 && !result->timed_out && !result->canceled,
      .result_text =
          [&] {
            std::string text = "{\"tool\":\"bash\",\"ok\":" +
                               json_bool(result->exit_code == 0 && !result->timed_out && !result->canceled) +
                               ",\"exit_code\":" + std::to_string(result->exit_code) +
                               ",\"timed_out\":" + json_bool(result->timed_out) +
                               ",\"canceled\":" + json_bool(result->canceled) +
                               ",\"truncated\":" + json_bool(result->truncated) +
                               ",\"byte_limited\":" + json_bool(result->byte_limited) +
                               ",\"line_limited\":" + json_bool(result->line_limited) +
                               ",\"total_bytes\":" + std::to_string(result->total_bytes) +
                               ",\"output_bytes\":" + std::to_string(result->output_bytes) +
                               ",\"total_lines\":" + std::to_string(result->total_lines) +
                               ",\"output_lines\":" + std::to_string(result->output_lines) +
                               ",\"omitted_lines\":" + std::to_string(result->omitted_lines) + ",\"output\":\"" +
                               ava::core::json::escape(result->output) + "\"";
            if (result->truncated && result->line_limited) {
              text += ",\"truncation_hint\":\"Increase max_lines to retain more command output.\"";
            } else if (result->truncated && result->byte_limited) {
              text += ",\"truncation_hint\":\"Increase max_bytes to retain longer output lines.\"";
            }
            append_spill_fields(text, result->spill_path, result->spill_truncated);
            text += "}";
            return text;
          }(),
      .payload =
          [&] {
            ava::agent::ToolResultPayload payload;
            payload.status =
                result->canceled ? ava::agent::ToolResultStatus::Canceled : ava::agent::ToolResultStatus::Success;
            return payload;
          }()};
}

ToolDispatchResult webfetch_result(ava::tools::ToolContext const& context, ProviderToolCall const& call)
{
  auto url = required_safe_string_arg(call.arguments_json, "url", call.name);
  if (!url) return tool_error_result(call, url.error());
  auto const format_text = ava::core::json::string_field(call.arguments_json, "format").value_or("markdown");
  ava::tools::WebFetchFormat format = ava::tools::WebFetchFormat::Markdown;
  if (format_text == "markdown") {
    format = ava::tools::WebFetchFormat::Markdown;
  } else if (format_text == "text") {
    format = ava::tools::WebFetchFormat::Text;
  } else if (format_text == "html") {
    format = ava::tools::WebFetchFormat::Html;
  } else {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "webfetch format is invalid");
    error.with_context("tool", call.name);
    error.with_context("format", format_text);
    return tool_error_result(call, error);
  }
  auto const tool_context = context_for_provider_tool(context, call);
  auto result = ava::tools::webfetch(
      tool_context, *url,
      ava::tools::WebFetchOptions{
          .max_bytes = optional_size_arg(call.arguments_json, "max_bytes", 1024 * 1024, 5 * 1024 * 1024),
          .offset_line = optional_size_arg(call.arguments_json, "offset", 1, 100000000),
          .max_lines = optional_size_arg(call.arguments_json, "limit", 200, 100000),
          .timeout_ms = static_cast<int>(optional_size_arg(call.arguments_json, "timeout_ms", 30000, 120000)),
          .format = format,
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
                     ",\"byte_limited\":" + json_bool(result->byte_limited) + ",\"line_limited\":" +
                     json_bool(result->line_limited) + ",\"total_bytes\":" + std::to_string(result->total_bytes) +
                     ",\"output_bytes\":" + std::to_string(result->output_bytes) + ",\"output_lines\":" +
                     std::to_string(result->output_lines) + ",\"start_line\":" + std::to_string(result->start_line) +
                     ",\"end_line\":" + std::to_string(result->end_line) +
                     ",\"total_lines\":" + std::to_string(result->total_lines) +
                     [&] {
                       std::string suffix;
                       if (result->next_offset_line > 0) {
                         suffix += ",\"next_offset\":" + std::to_string(result->next_offset_line);
                         suffix += ",\"next_offset_line\":" + std::to_string(result->next_offset_line);
                         suffix += ",\"truncation_hint\":\"Call webfetch again with offset=" +
                                   std::to_string(result->next_offset_line) + " to continue.\"";
                       } else if (result->byte_limited) {
                         suffix += ",\"truncation_hint\":\"Increase max_bytes to read more of this range.\"";
                       }
                       return suffix;
                     }() +
                     "}"};
}

ToolDispatchResult websearch_result(ava::tools::ToolContext const& context, ProviderToolCall const& call)
{
  auto query = required_safe_string_arg(call.arguments_json, "query", call.name);
  if (!query) return tool_error_result(call, query.error());
  auto context_max_chars = optional_size_arg(call.arguments_json, "context_max_chars", 0, 30000);
  if (context_max_chars == 0) {
    context_max_chars = optional_size_arg(call.arguments_json, "contextMaxCharacters", 10000, 30000);
  }
  auto const tool_context = context_for_provider_tool(context, call);
  auto result = ava::tools::websearch(
      tool_context, *query,
      ava::tools::WebSearchOptions{
          .max_results = optional_size_arg(call.arguments_json, "num_results", 8, 10),
          .context_max_chars = context_max_chars,
          .timeout_ms = static_cast<int>(optional_size_arg(call.arguments_json, "timeout_ms", 25000, 60000)),
      });
  if (!result) return tool_error_result(call, result.error());
  std::string text = "{\"tool\":\"websearch\",\"ok\":true,\"query\":\"" + ava::core::json::escape(result->query) +
                     "\",\"engine\":\"" + ava::core::json::escape(result->engine) + "\",\"results\":[";
  for (std::size_t index = 0; index < result->results.size(); ++index) {
    auto const& item = result->results[index];
    if (index > 0) text += ',';
    text += "{\"title\":\"" + ava::core::json::escape(item.title) + "\",\"url\":\"" +
            ava::core::json::escape(item.url) + "\",\"snippet\":\"" + ava::core::json::escape(item.snippet) + "\"}";
  }
  text += "],\"truncated\":" + json_bool(result->truncated) +
          ",\"total_results\":" + std::to_string(result->total_results) +
          ",\"output_chars\":" + std::to_string(result->output_chars) + "}";
  return ToolDispatchResult{.call_id = call.id, .name = call.name, .success = true, .result_text = std::move(text)};
}

ToolDispatchResult skill_result(ava::tools::ToolContext const& context, ProviderToolCall const& call)
{
  auto name = required_safe_string_arg(call.arguments_json, "name", call.name);
  if (!name) return tool_error_result(call, name.error());
  auto skills =
      ava::context::load_skills(ava::context::SkillLoadOptions{.workspace_root = context.workspace_dir,
                                                               .global_skill_dirs = context.skill_global_dirs,
                                                               .project_skill_dirs = context.skill_project_dirs});
  auto const match =
      std::ranges::find_if(skills.skills, [&](ava::context::LoadedSkill const& skill) { return skill.name == *name; });
  if (match == skills.skills.end()) {
    std::string available;
    for (auto const& skill : skills.skills) {
      if (!available.empty()) available += ", ";
      available += skill.name;
    }
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "skill not found");
    error.with_context("tool", call.name);
    error.with_context("skill", *name);
    error.with_context("available", available.empty() ? "none" : available);
    return tool_error_result(call, error);
  }

  auto tool_context = context_for_provider_tool(context, call);
  if (auto permission = ava::tools::ensure_permission(tool_context, ava::permissions::Operation::SkillLoad, match->path,
                                                      match->name, "skill", "skill loading requires permission");
      !permission) {
    return tool_error_result(call, permission.error());
  }
  auto sampled_files = ava::context::sample_skill_files(match->directory);
  auto content = ava::context::format_loaded_skill_for_tool(*match, sampled_files);
  return ToolDispatchResult{.call_id = call.id,
                            .name = call.name,
                            .success = true,
                            .result_text = "{\"tool\":\"skill\",\"ok\":true,\"name\":\"" +
                                           ava::core::json::escape(match->name) + "\",\"path\":\"" +
                                           ava::core::json::escape(match->path.string()) + "\",\"content\":\"" +
                                           ava::core::json::escape(content) + "\"}"};
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
  if (name == "list_directory") return list_directory_result;
  if (name == "grep") return grep_result;
  if (name == "bash") return bash_result;
  if (name == "webfetch") return webfetch_result;
  if (name == "websearch") return websearch_result;
  if (name == "skill") return skill_result;
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
    auto context = context_;
    context.permission_request_ids = std::make_shared<std::vector<std::string>>();
    auto result = tool->executor(context, normalized);
    if (context.permission_request_ids && !context.permission_request_ids->empty()) {
      result.payload.permission_request_ids = *context.permission_request_ids;
    }
    return with_tool_result_payload(std::move(result));
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
