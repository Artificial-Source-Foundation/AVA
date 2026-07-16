#include "sys.h"
#include "ava/agent/question.h"
#include "ava/agent/tool_dispatch_common.h"
#include "ava/agent/tool_dispatch_patch.h"
#include "ava/agent/tool_dispatcher.h"
#include "ava/agent/tool_registry.h"
#include "ava/agent/tool_result.h"
#include "ava/tools/bash_tool.h"
#include "ava/tools/lsp_tools.h"
#include "ava/tools/mutation_queue.h"
#include "ava/tools/search_tools.h"
#include "ava/tools/secure_workspace.h"
#include "ava/tools/webfetch_tool.h"
#include "ava/tools/websearch_tool.h"
#include "ava/plugin/diagnostics.h"
#include "ava/plugin/discovery.h"
#include "ava/plugin/enablement.h"
#include "ava/plugin/static_resources.h"
#include "ava/plugin/tool_broker.h"
#include "ava/mcp/tool_broker.h"
#include "ava/context/skill_loader.h"
#include "ava/core/json.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>
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
constexpr std::size_t kMaxLspProviderQueryBytes = 1024;
constexpr std::size_t kMaxTaskDescriptionBytes = 256;
constexpr std::size_t kMaxTaskPromptBytes = 64 * 1024;
constexpr std::size_t kMaxTaskSubagentTypeBytes = 128;
constexpr std::size_t kMaxTaskIdBytes = 256;
constexpr std::size_t kMaxTaskCommandBytes = 1024;

using namespace ava::agent::tool_dispatch;

bool is_lsp_diagnostics_metadata(ToolMetadata const& tool)
{
  return tool.name == std::string_view("lsp_diagnostics") || tool.name == std::string_view("lsp_document_symbols") ||
         tool.name == std::string_view("lsp_workspace_symbols") || tool.name == std::string_view("lsp_definition") ||
         tool.name == std::string_view("lsp_references");
}

ToolDispatchResult lsp_error_result(ProviderToolCall const& call, ava::core::Error const& error)
{
  if (error.message().find("canceled") != std::string::npos || error.message().find("cancelled") != std::string::npos)
  {
    auto redacted = ava::core::Error(error.category(), "LSP query canceled");
    redacted.with_context("tool", call.name);
    return tool_error_result(call, redacted);
  }
  auto redacted = ava::core::Error(error.category(), "LSP query failed");
  redacted.with_context("tool", call.name);
  return tool_error_result(call, redacted);
}

std::string lsp_range_json(ava::lsp::Range const& range)
{
  return "{\"start_line\":" + std::to_string(range.start_line) + ",\"start_column\":" + std::to_string(range.start_column) +
         ",\"end_line\":" + std::to_string(range.end_line) + ",\"end_column\":" + std::to_string(range.end_column) + "}";
}

std::string lsp_path_for_result(ava::tools::ToolContext const& context, std::filesystem::path const& path)
{
  std::error_code error;
  auto relative = std::filesystem::relative(path, context.workspace_dir, error);
  if (!error && !relative.empty())
  {
    auto const native = relative.native();
    if (native != ".." && native.rfind("../", 0) != 0)
      return relative.generic_string();
  }
  return path.generic_string();
}

std::string lsp_symbol_entry_json(ava::tools::ToolContext const& context, ava::lsp::Symbol const& symbol)
{
  return "{\"name\":\"" + ava::core::json::escape(symbol.name) + "\",\"kind\":" + std::to_string(symbol.kind) + ",\"path\":\"" +
         ava::core::json::escape(lsp_path_for_result(context, symbol.path)) + "\",\"range\":" + lsp_range_json(symbol.range) + ",\"container\":\"" +
         ava::core::json::escape(symbol.container) + "\"}";
}

std::string lsp_location_entry_json(ava::tools::ToolContext const& context, ava::lsp::Location const& location)
{
  return "{\"path\":\"" + ava::core::json::escape(lsp_path_for_result(context, location.path)) + "\",\"range\":" + lsp_range_json(location.range) + "}";
}

std::string xml_escape(std::string_view value)
{
  std::string escaped;
  escaped.reserve(value.size());
  for (char const ch : value)
  {
    switch (ch)
    {
      case '&':
        escaped += "&amp;";
        break;
      case '<':
        escaped += "&lt;";
        break;
      case '>':
        escaped += "&gt;";
        break;
      case '"':
        escaped += "&quot;";
        break;
      case '\'':
        escaped += "&apos;";
        break;
      default:
        escaped.push_back(ch);
        break;
    }
  }
  return escaped;
}

ava::plugin::PluginDiscoveryOptions plugin_discovery_options_for_context(ava::tools::ToolContext const& context)
{
  auto options = ava::plugin::default_plugin_discovery_options(context.workspace_dir);
  if (!context.plugin_global_plugins_dir.empty())
    options.global_plugins_dir = context.plugin_global_plugins_dir;
  if (!context.plugin_project_plugins_dir.empty())
    options.project_plugins_dir = context.plugin_project_plugins_dir;
  if (!context.include_project_plugins)
    options.project_plugins_dir = std::filesystem::path{};
  return options;
}

std::filesystem::path plugin_enablement_file_for_context(ava::tools::ToolContext const& context)
{
  if (!context.plugin_enablement_file.empty())
    return context.plugin_enablement_file;
  return ava::plugin::default_plugin_enablement_file();
}

std::vector<ava::context::DeclaredSkillFileOptions> declared_plugin_skill_files(ava::plugin::PluginDiagnostics const& diagnostics)
{
  std::vector<ava::context::DeclaredSkillFileOptions> files;
  for (auto const& skill : ava::plugin::enabled_plugin_static_skill_files(diagnostics))
  {
    files.push_back(ava::context::DeclaredSkillFileOptions{
        .path = skill.path, .name = skill.name, .description = skill.description, .source_type = ava::context::SkillSourceType::Plugin});
  }
  return files;
}

ava::core::VoidResult reject_oversized_task_arg(std::string_view value, std::string_view field, std::size_t max_bytes, std::string_view tool_name)
{
  if (value.size() <= max_bytes)
    return {};
  auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "task argument is too long");
  error.with_context("tool", std::string(tool_name));
  error.with_context("argument", std::string(field));
  error.with_context("max_bytes", std::to_string(max_bytes));
  return std::unexpected(std::move(error));
}

ava::core::Result<std::optional<std::string>> optional_task_string_arg(std::string_view arguments, std::string_view field, std::size_t max_bytes,
                                                                       std::string_view tool_name)
{
  auto value = ava::core::json::string_field(arguments, field);
  if (!value)
    return std::optional<std::string>{};
  if (auto safe = reject_control_arg(*value, field, tool_name); !safe)
    return std::unexpected(std::move(safe.error()));
  if (auto bounded = reject_oversized_task_arg(*value, field, max_bytes, tool_name); !bounded)
  {
    return std::unexpected(std::move(bounded.error()));
  }
  return std::optional<std::string>{std::move(*value)};
}

ava::core::Result<SubagentDefinition> selected_subagent_definition(ava::tools::ToolContext const& context, std::string_view subagent_type,
                                                                   std::string_view tool_name)
{
  auto subagents = context.subagents.empty() ? builtin_subagents() : context.subagents;
  auto const* match = find_subagent(subagents, subagent_type);
  if (match)
    return *match;
  auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "unsupported subagent type");
  error.with_context("tool", std::string(tool_name));
  error.with_context("subagent_type", std::string(subagent_type));
  error.with_context("supported", subagent_names_csv(subagents));
  return std::unexpected(std::move(error));
}

template <typename Entry, typename Serializer>
void append_lsp_entries(std::string& text, std::vector<Entry> const& entries, Serializer serializer, bool& truncated)
{
  for (std::size_t index = 0; index < entries.size(); ++index)
  {
    if (index >= kMaxLspProviderDiagnostics)
    {
      truncated = true;
      break;
    }
    auto entry = serializer(entries[index]);
    if (text.size() + entry.size() + 96 > kMaxLspProviderJsonBytes)
    {
      truncated = true;
      break;
    }
    if (index > 0)
      text += ',';
    text += std::move(entry);
  }
}

ava::core::Result<int> required_nonnegative_int_arg(std::string_view arguments, std::string_view field, std::string_view tool_name)
{
  auto value = ava::core::json::integer_field(arguments, field);
  if (!value || *value < 0 || *value > static_cast<long long>(std::numeric_limits<int>::max()))
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "tool integer argument is invalid");
    error.with_context("tool", std::string(tool_name));
    error.with_context("field", std::string(field));
    return std::unexpected(std::move(error));
  }
  return static_cast<int>(*value);
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
  if (answer.selected_options.size() > kMaxQuestionAnswerSelectedOptions)
  {
    auto error = question_answer_error(tool_name, "selected_options", "question answer has too many selected options");
    error.with_context("max_options", std::to_string(kMaxQuestionAnswerSelectedOptions));
    return std::unexpected(std::move(error));
  }
  for (std::size_t index = 0; index < answer.selected_options.size(); ++index)
  {
    if (answer.selected_options[index].size() > kMaxQuestionAnswerStringBytes)
    {
      auto error = question_answer_error(tool_name, "selected_options", "question answer selected option is too long");
      error.with_context("index", std::to_string(index));
      error.with_context("max_bytes", std::to_string(kMaxQuestionAnswerStringBytes));
      return std::unexpected(std::move(error));
    }
  }
  if (answer.custom_text.size() > kMaxQuestionAnswerStringBytes)
  {
    auto error = question_answer_error(tool_name, "custom_text", "question answer custom text is too long");
    error.with_context("max_bytes", std::to_string(kMaxQuestionAnswerStringBytes));
    return std::unexpected(std::move(error));
  }
  return {};
}

ToolDispatchResult read_file_result(ava::tools::ToolContext const& context, ProviderToolCall const& call)
{
  auto path = required_safe_string_arg(call.arguments_json, "path", call.name);
  if (!path)
    return tool_error_result(call, path.error());
  auto const tool_context = context_for_provider_tool(context, call);
  auto result = ava::tools::read_file(tool_context, workspace_path(context, *path),
                                      ava::tools::ReadOptions{.max_bytes = optional_size_arg(call.arguments_json, "max_bytes", 50 * 1024, 512 * 1024),
                                                              .offset_line = optional_size_arg(call.arguments_json, "offset", 1, 100000000),
                                                              .max_lines = optional_size_arg(call.arguments_json, "limit", 200, 100000)});
  if (!result)
    return tool_error_result(call, result.error());
  std::string text = "{\"tool\":\"read_file\",\"ok\":true,\"path\":\"" + ava::core::json::escape(*path) + "\",\"content\":\"" +
                     ava::core::json::escape(result->content) + "\",\"truncated\":" + json_bool(result->truncated) +
                     ",\"byte_limited\":" + json_bool(result->byte_limited) + ",\"line_limited\":" + json_bool(result->line_limited);
  if (result->totals_known)
    text += ",\"total_bytes\":" + std::to_string(result->total_bytes);
  text += ",\"output_bytes\":" + std::to_string(result->output_bytes) + ",\"output_lines\":" + std::to_string(result->output_lines) +
          ",\"start_line\":" + std::to_string(result->start_line) + ",\"end_line\":" + std::to_string(result->end_line);
  if (result->totals_known)
    text += ",\"total_lines\":" + std::to_string(result->total_lines);
  if (result->next_offset_line > 0)
  {
    text += ",\"next_offset\":" + std::to_string(result->next_offset_line);
    text += ",\"next_offset_line\":" + std::to_string(result->next_offset_line);
    text += ",\"truncation_hint\":\"Call read_file again with offset=" + std::to_string(result->next_offset_line) + " to continue.\"";
  }
  else if (result->byte_limited)
  {
    text += ",\"truncation_hint\":\"Increase max_bytes to read more of this range.\"";
  }
  text += "}";
  return ToolDispatchResult{.call_id = call.id, .name = call.name, .success = true, .result_text = std::move(text)};
}

ToolDispatchResult write_file_result(ava::tools::ToolContext const& context, ProviderToolCall const& call)
{
  auto path = required_safe_string_arg(call.arguments_json, "path", call.name);
  if (!path)
    return tool_error_result(call, path.error());
  auto content = required_text_arg(call.arguments_json, "content", call.name);
  if (!content)
    return tool_error_result(call, content.error());
  auto const tool_context = context_for_provider_tool(context, call);
  auto result = ava::tools::write_file(tool_context, workspace_path(context, *path), *content);
  if (!result)
    return tool_error_result(call, result.error());
  return ToolDispatchResult{.call_id = call.id,
                            .name = call.name,
                            .success = true,
                            .result_text = "{\"tool\":\"write_file\",\"ok\":true,\"path\":\"" + ava::core::json::escape(result->path.generic_string()) +
                                           "\",\"bytes_written\":" + std::to_string(result->bytes_written) + ",\"diff\":\"" +
                                           ava::core::json::escape(result->diff) + "\",\"diff_truncated\":" + json_bool(result->diff_truncated) + "}"};
}

ToolDispatchResult edit_file_result(ava::tools::ToolContext const& context, ProviderToolCall const& call)
{
  auto path = required_safe_string_arg(call.arguments_json, "path", call.name);
  if (!path)
    return tool_error_result(call, path.error());
  auto old_text = required_text_arg(call.arguments_json, "old_text", call.name);
  if (!old_text)
    return tool_error_result(call, old_text.error());
  auto new_text = required_text_arg(call.arguments_json, "new_text", call.name);
  if (!new_text)
    return tool_error_result(call, new_text.error());
  auto const tool_context = context_for_provider_tool(context, call);
  auto result = ava::tools::edit_file(tool_context, workspace_path(context, *path), *old_text, *new_text);
  if (!result)
    return tool_error_result(call, result.error());
  return ToolDispatchResult{.call_id = call.id,
                            .name = call.name,
                            .success = true,
                            .result_text = "{\"tool\":\"edit_file\",\"ok\":true,\"path\":\"" + ava::core::json::escape(result->path.generic_string()) +
                                           "\",\"bytes_written\":" + std::to_string(result->bytes_written) + ",\"diff\":\"" +
                                           ava::core::json::escape(result->diff) + "\",\"diff_truncated\":" + json_bool(result->diff_truncated) +
                                           ",\"line_endings\":\"" + ava::core::json::escape(result->line_endings) +
                                           "\",\"utf8_bom\":" + json_bool(result->had_utf8_bom) + "}"};
}

ToolDispatchResult glob_result(ava::tools::ToolContext const& context, ProviderToolCall const& call)
{
  auto pattern = required_safe_string_arg(call.arguments_json, "pattern", call.name);
  if (!pattern)
    return tool_error_result(call, pattern.error());
  if (auto no_ignore_allowed = reject_provider_no_ignore(call.arguments_json, call.name); !no_ignore_allowed)
  {
    return tool_error_result(call, no_ignore_allowed.error());
  }
  auto const tool_context = context_for_provider_tool(context, call);
  auto result = ava::tools::glob_files(tool_context, *pattern,
                                       ava::tools::GlobOptions{.max_results = optional_size_arg(call.arguments_json, "max_results", 2000, 10000)});
  if (!result)
    return tool_error_result(call, result.error());
  std::string text = "{\"tool\":\"glob\",\"ok\":true,\"pattern\":\"" + ava::core::json::escape(*pattern) + "\",\"paths\":[";
  for (std::size_t index = 0; index < result->paths.size(); ++index)
  {
    if (index > 0)
      text += ',';
    text += "\"" + ava::core::json::escape(result->paths[index].generic_string()) + "\"";
  }
  text += "],\"truncated\":" + json_bool(result->truncated) + ",\"total_matches\":" + std::to_string(result->total_matches);
  append_spill_fields(text, result->spill_path, result->spill_truncated);
  text += "}";
  return ToolDispatchResult{.call_id = call.id, .name = call.name, .success = true, .result_text = text};
}

ToolDispatchResult list_directory_result(ava::tools::ToolContext const& context, ProviderToolCall const& call)
{
  auto path_value = ava::core::json::string_field(call.arguments_json, "path");
  if (path_value)
  {
    if (auto safe = reject_control_arg(*path_value, "path", call.name); !safe)
    {
      return tool_error_result(call, safe.error());
    }
  }
  auto const path = path_value.value_or(".");
  auto const tool_context = context_for_provider_tool(context, call);
  auto result = ava::tools::list_directory(tool_context, workspace_path(context, path),
                                           ava::tools::ListDirectoryOptions{.max_entries = optional_size_arg(call.arguments_json, "max_entries", 500, 5000)});
  if (!result)
    return tool_error_result(call, result.error());
  std::string text = "{\"tool\":\"list_directory\",\"ok\":true,\"path\":\"" + ava::core::json::escape(path) + "\",\"entries\":[";
  for (std::size_t index = 0; index < result->entries.size(); ++index)
  {
    auto const& entry = result->entries[index];
    if (index > 0)
      text += ',';
    text += "{\"name\":\"" + ava::core::json::escape(entry.name) + "\",\"type\":\"" + std::string(entry.directory ? "directory" : "file") +
            "\",\"size\":" + std::to_string(entry.size) + "}";
  }
  text += "],\"truncated\":" + json_bool(result->truncated) + ",\"total_entries\":" + std::to_string(result->total_entries) + "}";
  return ToolDispatchResult{.call_id = call.id, .name = call.name, .success = true, .result_text = std::move(text)};
}

ToolDispatchResult grep_result(ava::tools::ToolContext const& context, ProviderToolCall const& call)
{
  auto pattern = required_text_arg(call.arguments_json, "pattern", call.name);
  if (!pattern)
    return tool_error_result(call, pattern.error());
  auto const include_value = ava::core::json::string_field(call.arguments_json, "include");
  if (include_value)
  {
    if (auto safe = reject_control_arg(*include_value, "include", call.name); !safe)
    {
      return tool_error_result(call, safe.error());
    }
  }
  auto const include = include_value.value_or("**/*");
  if (auto no_ignore_allowed = reject_provider_no_ignore(call.arguments_json, call.name); !no_ignore_allowed)
  {
    return tool_error_result(call, no_ignore_allowed.error());
  }
  auto literal = optional_bool_arg(call.arguments_json, "literal", true, call.name);
  if (!literal)
    return tool_error_result(call, literal.error());
  auto case_insensitive = optional_bool_arg(call.arguments_json, "case_insensitive", false, call.name);
  if (!case_insensitive)
    return tool_error_result(call, case_insensitive.error());
  auto const tool_context = context_for_provider_tool(context, call);
  auto result = ava::tools::grep_files(
      tool_context, *pattern, include,
      ava::tools::GrepOptions{
          .max_matches = optional_size_arg(call.arguments_json, "max_matches", 2000, 10000), .literal = *literal, .case_insensitive = *case_insensitive});
  if (!result)
    return tool_error_result(call, result.error());
  std::string text = "{\"tool\":\"grep\",\"ok\":true,\"pattern\":\"" + ava::core::json::escape(*pattern) + "\",\"include\":\"" +
                     ava::core::json::escape(include) + "\",\"literal\":" + json_bool(*literal) + ",\"case_insensitive\":" + json_bool(*case_insensitive) +
                     ",\"matches\":[";
  for (std::size_t index = 0; index < result->matches.size(); ++index)
  {
    auto const& match = result->matches[index];
    if (index > 0)
      text += ',';
    text += "{\"path\":\"" + ava::core::json::escape(match.path.generic_string()) + "\",\"line_number\":" + std::to_string(match.line_number) + ",\"line\":\"" +
            ava::core::json::escape(match.line) + "\",\"line_truncated\":" + json_bool(match.line_truncated) + "}";
  }
  text += "],\"truncated\":" + json_bool(result->truncated) + ",\"total_matches\":" + std::to_string(result->total_matches);
  append_spill_fields(text, result->spill_path, result->spill_truncated);
  text += "}";
  return ToolDispatchResult{.call_id = call.id, .name = call.name, .success = true, .result_text = text};
}

ToolDispatchResult bash_result(ava::tools::ToolContext const& context, ProviderToolCall const& call)
{
  auto command = required_safe_string_arg(call.arguments_json, "command", call.name);
  if (!command)
    return tool_error_result(call, command.error());
  auto max_lines = optional_size_arg(call.arguments_json, "max_lines", 0, 100000);
  if (max_lines == 0)
    max_lines = optional_size_arg(call.arguments_json, "limit", 200, 100000);
  auto const tool_context = context_for_provider_tool(context, call);
  auto result =
      ava::tools::run_bash(tool_context, *command,
                           ava::tools::BashOptions{.timeout = std::chrono::milliseconds(optional_size_arg(call.arguments_json, "timeout_ms", 30000, 120000)),
                                                   .max_bytes = optional_size_arg(call.arguments_json, "max_bytes", 50 * 1024, 512 * 1024),
                                                   .max_lines = max_lines});
  if (!result)
    return tool_error_result(call, result.error());
  return ToolDispatchResult{.call_id = call.id,
                            .name = call.name,
                            .success = result->exit_code == 0 && !result->timed_out && !result->canceled,
                            .result_text =
                                [&] {
                                  std::string text =
                                      "{\"tool\":\"bash\",\"ok\":" + json_bool(result->exit_code == 0 && !result->timed_out && !result->canceled) +
                                      ",\"exit_code\":" + std::to_string(result->exit_code) + ",\"timed_out\":" + json_bool(result->timed_out) +
                                      ",\"canceled\":" + json_bool(result->canceled) + ",\"truncated\":" + json_bool(result->truncated) +
                                      ",\"byte_limited\":" + json_bool(result->byte_limited) + ",\"line_limited\":" + json_bool(result->line_limited);
                                  if (result->totals_known)
                                    text += ",\"total_bytes\":" + std::to_string(result->total_bytes);
                                  text += ",\"output_bytes\":" + std::to_string(result->output_bytes);
                                  if (result->totals_known)
                                  {
                                    text += ",\"total_lines\":" + std::to_string(result->total_lines);
                                  }
                                  text += ",\"output_lines\":" + std::to_string(result->output_lines);
                                  if (result->totals_known)
                                    text += ",\"omitted_lines\":" + std::to_string(result->omitted_lines);
                                  text += ",\"output\":\"" + ava::core::json::escape(result->output) + "\"";
                                  if (result->truncated && result->line_limited)
                                  {
                                    text += ",\"truncation_hint\":\"Increase max_lines to retain more command output.\"";
                                  }
                                  else if (result->truncated && result->byte_limited)
                                  {
                                    text += ",\"truncation_hint\":\"Increase max_bytes to retain longer output lines.\"";
                                  }
                                  append_spill_fields(text, result->spill_path, result->spill_truncated);
                                  text += "}";
                                  return text;
                                }(),
                            .payload =
                                [&] {
                                  ava::agent::ToolResultPayload payload;
                                  payload.status = result->canceled ? ava::agent::ToolResultStatus::Canceled : ava::agent::ToolResultStatus::Success;
                                  return payload;
                                }()};
}

ToolDispatchResult webfetch_result(ava::tools::ToolContext const& context, ProviderToolCall const& call)
{
  auto url = required_safe_string_arg(call.arguments_json, "url", call.name);
  if (!url)
    return tool_error_result(call, url.error());
  auto const format_text = ava::core::json::string_field(call.arguments_json, "format").value_or("markdown");
  ava::tools::WebFetchFormat format = ava::tools::WebFetchFormat::Markdown;
  if (format_text == "markdown")
  {
    format = ava::tools::WebFetchFormat::Markdown;
  }
  else if (format_text == "text")
  {
    format = ava::tools::WebFetchFormat::Text;
  }
  else if (format_text == "html")
  {
    format = ava::tools::WebFetchFormat::Html;
  }
  else
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "webfetch format is invalid");
    error.with_context("tool", call.name);
    error.with_context("format", format_text);
    return tool_error_result(call, error);
  }
  auto const tool_context = context_for_provider_tool(context, call);
  auto result = ava::tools::webfetch(tool_context, *url,
                                     ava::tools::WebFetchOptions{
                                         .max_bytes = optional_size_arg(call.arguments_json, "max_bytes", 1024 * 1024, 5 * 1024 * 1024),
                                         .offset_line = optional_size_arg(call.arguments_json, "offset", 1, 100000000),
                                         .max_lines = optional_size_arg(call.arguments_json, "limit", 200, 100000),
                                         .timeout_ms = static_cast<int>(optional_size_arg(call.arguments_json, "timeout_ms", 30000, 120000)),
                                         .format = format,
                                     });
  if (!result)
    return tool_error_result(call, result.error());
  return ToolDispatchResult{
      .call_id = call.id,
      .name = call.name,
      .success = true,
      .result_text = "{\"tool\":\"webfetch\",\"ok\":true,\"url\":\"" + ava::core::json::escape(result->url) +
                     "\",\"status_code\":" + std::to_string(result->status_code) + ",\"content_type\":\"" + ava::core::json::escape(result->content_type) +
                     "\",\"content\":\"" + ava::core::json::escape(result->content) + "\",\"truncated\":" + json_bool(result->truncated) +
                     ",\"byte_limited\":" + json_bool(result->byte_limited) + ",\"line_limited\":" + json_bool(result->line_limited) +
                     ",\"total_bytes\":" + std::to_string(result->total_bytes) + ",\"output_bytes\":" + std::to_string(result->output_bytes) +
                     ",\"output_lines\":" + std::to_string(result->output_lines) + ",\"start_line\":" + std::to_string(result->start_line) +
                     ",\"end_line\":" + std::to_string(result->end_line) + ",\"total_lines\":" + std::to_string(result->total_lines) +
                     [&] {
                       std::string suffix;
                       if (result->next_offset_line > 0)
                       {
                         suffix += ",\"next_offset\":" + std::to_string(result->next_offset_line);
                         suffix += ",\"next_offset_line\":" + std::to_string(result->next_offset_line);
                         suffix += ",\"truncation_hint\":\"Call webfetch again with offset=" + std::to_string(result->next_offset_line) + " to continue.\"";
                       }
                       else if (result->byte_limited)
                       {
                         suffix += ",\"truncation_hint\":\"Increase max_bytes to read more of this range.\"";
                       }
                       return suffix;
                     }() +
                     "}"};
}

ToolDispatchResult websearch_result(ava::tools::ToolContext const& context, ProviderToolCall const& call)
{
  auto query = required_safe_string_arg(call.arguments_json, "query", call.name);
  if (!query)
    return tool_error_result(call, query.error());
  auto context_max_chars = optional_size_arg(call.arguments_json, "context_max_chars", 0, 30000);
  if (context_max_chars == 0)
  {
    context_max_chars = optional_size_arg(call.arguments_json, "contextMaxCharacters", 10000, 30000);
  }
  auto const tool_context = context_for_provider_tool(context, call);
  auto result = ava::tools::websearch(tool_context, *query,
                                      ava::tools::WebSearchOptions{
                                          .max_results = optional_size_arg(call.arguments_json, "num_results", 8, 10),
                                          .context_max_chars = context_max_chars,
                                          .timeout_ms = static_cast<int>(optional_size_arg(call.arguments_json, "timeout_ms", 25000, 60000)),
                                      });
  if (!result)
    return tool_error_result(call, result.error());
  std::string text = "{\"tool\":\"websearch\",\"ok\":true,\"query\":\"" + ava::core::json::escape(result->query) + "\",\"engine\":\"" +
                     ava::core::json::escape(result->engine) + "\",\"results\":[";
  for (std::size_t index = 0; index < result->results.size(); ++index)
  {
    auto const& item = result->results[index];
    if (index > 0)
      text += ',';
    text += "{\"title\":\"" + ava::core::json::escape(item.title) + "\",\"url\":\"" + ava::core::json::escape(item.url) + "\",\"snippet\":\"" +
            ava::core::json::escape(item.snippet) + "\"}";
  }
  text += "],\"truncated\":" + json_bool(result->truncated) + ",\"total_results\":" + std::to_string(result->total_results) +
          ",\"output_chars\":" + std::to_string(result->output_chars) + "}";
  return ToolDispatchResult{.call_id = call.id, .name = call.name, .success = true, .result_text = std::move(text)};
}

ToolDispatchResult skill_result(ava::tools::ToolContext const& context, ProviderToolCall const& call)
{
  auto name = required_safe_string_arg(call.arguments_json, "name", call.name);
  if (!name)
    return tool_error_result(call, name.error());
  auto plugin_diagnostics = ava::plugin::collect_plugin_diagnostics(plugin_discovery_options_for_context(context), plugin_enablement_file_for_context(context),
                                                                    context.workspace_dir);
  auto skills = ava::context::load_skills(ava::context::SkillLoadOptions{.workspace_root = context.workspace_dir,
                                                                         .global_skill_dirs = context.skill_global_dirs,
                                                                         .project_skill_dirs = context.skill_project_dirs,
                                                                         .declared_skill_files = declared_plugin_skill_files(plugin_diagnostics),
                                                                         .include_project_skills = context.include_project_skills});
  auto const match = std::ranges::find_if(skills.skills, [&](ava::context::LoadedSkill const& skill) { return skill.name == *name; });
  if (match == skills.skills.end())
  {
    std::string available;
    for (auto const& skill : skills.skills)
    {
      if (!available.empty())
        available += ", ";
      available += skill.name;
    }
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "skill not found");
    error.with_context("tool", call.name);
    error.with_context("skill", *name);
    error.with_context("available", available.empty() ? "none" : available);
    return tool_error_result(call, error);
  }

  auto tool_context = context_for_provider_tool(context, call);
  if (auto permission = ava::tools::ensure_permission(tool_context, ava::permissions::Operation::SkillLoad, match->path, match->name, "skill",
                                                      "skill loading requires permission");
      !permission)
  {
    return tool_error_result(call, permission.error());
  }
  auto sampled_files = ava::context::sample_skill_files(match->directory);
  auto content = ava::context::format_loaded_skill_for_tool(*match, sampled_files);
  return ToolDispatchResult{.call_id = call.id,
                            .name = call.name,
                            .success = true,
                            .result_text = "{\"tool\":\"skill\",\"ok\":true,\"name\":\"" + ava::core::json::escape(match->name) + "\",\"path\":\"" +
                                           ava::core::json::escape(match->path.string()) + "\",\"content\":\"" + ava::core::json::escape(content) + "\"}"};
}

ToolDispatchResult task_result(ava::tools::ToolContext const& context, ProviderToolCall const& call)
{
  auto description = required_safe_string_arg(call.arguments_json, "description", call.name);
  if (!description)
    return tool_error_result(call, description.error());
  if (auto bounded = reject_oversized_task_arg(*description, "description", kMaxTaskDescriptionBytes, call.name); !bounded)
  {
    return tool_error_result(call, bounded.error());
  }
  auto prompt = required_text_arg(call.arguments_json, "prompt", call.name);
  if (!prompt)
    return tool_error_result(call, prompt.error());
  if (auto bounded = reject_oversized_task_arg(*prompt, "prompt", kMaxTaskPromptBytes, call.name); !bounded)
  {
    return tool_error_result(call, bounded.error());
  }
  auto subagent_type = required_safe_string_arg(call.arguments_json, "subagent_type", call.name);
  if (!subagent_type)
    return tool_error_result(call, subagent_type.error());
  if (auto bounded = reject_oversized_task_arg(*subagent_type, "subagent_type", kMaxTaskSubagentTypeBytes, call.name); !bounded)
  {
    return tool_error_result(call, bounded.error());
  }
  auto subagent = selected_subagent_definition(context, *subagent_type, call.name);
  if (!subagent)
    return tool_error_result(call, subagent.error());
  auto task_id = optional_task_string_arg(call.arguments_json, "task_id", kMaxTaskIdBytes, call.name);
  if (!task_id)
    return tool_error_result(call, task_id.error());
  auto command = optional_task_string_arg(call.arguments_json, "command", kMaxTaskCommandBytes, call.name);
  if (!command)
    return tool_error_result(call, command.error());
  auto background = optional_bool_arg(call.arguments_json, "background", false, call.name);
  if (!background)
    return tool_error_result(call, background.error());
  if (!context.task_subagent_runner)
  {
    return simple_error_result(call, ava::core::ErrorCategory::Tool, "task subagent runner is unavailable");
  }

  auto tool_context = context_for_provider_tool(context, call);
  if (auto permission = ava::tools::ensure_permission(tool_context, ava::permissions::Operation::TaskRun, context.workspace_dir, *subagent_type, "task",
                                                      "subagent task execution requires permission");
      !permission)
  {
    return tool_error_result(call, permission.error());
  }

  auto run = context.task_subagent_runner(ava::tools::TaskSubagentRequest{.description = *description,
                                                                          .prompt = *prompt,
                                                                          .subagent_type = *subagent_type,
                                                                          .subagent_system_prompt = subagent->system_prompt,
                                                                          .tool_preset = subagent->tool_preset,
                                                                          .task_id = *task_id,
                                                                          .command = command->value_or(""),
                                                                          .background = *background});
  if (!run)
    return tool_error_result(call, run.error());

  auto const state = run->state.empty() ? std::string("completed") : run->state;
  auto const summary = state == "running" ? std::string("Background subagent task started: ") : std::string("Subagent task completed: ");
  auto const job_attr = run->job_id.empty() ? std::string{} : std::string(" job_id=\"") + xml_escape(run->job_id) + "\"";
  auto const content = std::string("<task id=\"") + xml_escape(run->task_id) + "\"" + job_attr + " state=\"" + xml_escape(state) + "\">" + "<summary>" +
                       summary + xml_escape(*description) + "</summary>" + "<task_result>" + xml_escape(run->final_text) + "</task_result></task>";
  std::string text = "{\"tool\":\"task\",\"ok\":true,\"task_id\":\"" + ava::core::json::escape(run->task_id) + "\",\"subagent_type\":\"" +
                     ava::core::json::escape(run->subagent_type) + "\",\"description\":\"" + ava::core::json::escape(*description) + "\",\"session_path\":\"" +
                     ava::core::json::escape(run->session_path.generic_string()) + "\",\"state\":\"" + ava::core::json::escape(state) + "\"" +
                     (run->job_id.empty() ? std::string{} : ",\"job_id\":\"" + ava::core::json::escape(run->job_id) + "\"") + ",\"stop_reason\":\"" +
                     ava::core::json::escape(run->stop_reason) + "\",\"provider_iterations\":" + std::to_string(run->provider_iterations) +
                     ",\"tool_calls\":" + std::to_string(run->tool_calls) + ",\"tool_iterations\":" + std::to_string(run->tool_iterations) +
                     ",\"task_result\":\"" + ava::core::json::escape(run->final_text) + "\",\"content\":\"" + ava::core::json::escape(content) + "\"}";
  return ToolDispatchResult{.call_id = call.id, .name = call.name, .success = true, .result_text = std::move(text)};
}

ToolDispatchResult lsp_diagnostics_result(ava::tools::ToolContext const& context, ProviderToolCall const& call)
{
  auto path = required_safe_string_arg(call.arguments_json, "path", call.name);
  if (!path)
    return tool_error_result(call, path.error());
  if (path->size() > kMaxLspProviderPathBytes)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "lsp_diagnostics path is too long");
    error.with_context("max_bytes", std::to_string(kMaxLspProviderPathBytes));
    return tool_error_result(call, error);
  }
  auto const tool_context = context_for_provider_tool(context, call);
  auto result = ava::tools::lsp_diagnostics(tool_context, workspace_path(context, *path));
  if (!result)
    return lsp_error_result(call, result.error());

  std::string text = "{\"tool\":\"lsp_diagnostics\",\"ok\":true,\"path\":\"" + ava::core::json::escape(*path) + "\",\"diagnostics\":[";
  auto const total_diagnostics = result->diagnostics.size();
  bool truncated = false;
  for (std::size_t index = 0; index < result->diagnostics.size(); ++index)
  {
    if (index >= kMaxLspProviderDiagnostics)
    {
      truncated = true;
      break;
    }
    auto const& diagnostic = result->diagnostics[index];
    auto entry = std::string{"{\"severity\":"} + std::to_string(diagnostic.severity) + ",\"message\":\"" + ava::core::json::escape(diagnostic.message) +
                 "\",\"line\":" + std::to_string(diagnostic.line) + ",\"column\":" + std::to_string(diagnostic.column) + ",\"code\":\"" +
                 ava::core::json::escape(diagnostic.code) + "\"}";
    if (text.size() + entry.size() + 80 > kMaxLspProviderJsonBytes)
    {
      truncated = true;
      break;
    }
    if (index > 0)
      text += ',';
    text += std::move(entry);
  }
  text += "],\"truncated\":" + json_bool(truncated) + ",\"total_diagnostics\":" + std::to_string(total_diagnostics) + "}";
  return ToolDispatchResult{.call_id = call.id, .name = call.name, .success = true, .result_text = std::move(text)};
}

ToolDispatchResult lsp_document_symbols_result(ava::tools::ToolContext const& context, ProviderToolCall const& call)
{
  auto path = required_safe_string_arg(call.arguments_json, "path", call.name);
  if (!path)
    return tool_error_result(call, path.error());
  if (path->size() > kMaxLspProviderPathBytes)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "lsp_document_symbols path is too long");
    error.with_context("max_bytes", std::to_string(kMaxLspProviderPathBytes));
    return tool_error_result(call, error);
  }
  auto const tool_context = context_for_provider_tool(context, call);
  auto result = ava::tools::lsp_document_symbols(tool_context, workspace_path(context, *path));
  if (!result)
    return lsp_error_result(call, result.error());

  std::string text = "{\"tool\":\"lsp_document_symbols\",\"ok\":true,\"path\":\"" + ava::core::json::escape(*path) + "\",\"symbols\":[";
  bool truncated = false;
  append_lsp_entries(text, result->symbols, [&](ava::lsp::Symbol const& symbol) { return lsp_symbol_entry_json(context, symbol); }, truncated);
  text += "],\"truncated\":" + json_bool(truncated) + ",\"total_symbols\":" + std::to_string(result->symbols.size()) + "}";
  return ToolDispatchResult{.call_id = call.id, .name = call.name, .success = true, .result_text = std::move(text)};
}

ToolDispatchResult lsp_workspace_symbols_result(ava::tools::ToolContext const& context, ProviderToolCall const& call)
{
  auto query = required_safe_string_arg(call.arguments_json, "query", call.name);
  if (!query)
    return tool_error_result(call, query.error());
  if (query->size() > kMaxLspProviderQueryBytes)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "lsp_workspace_symbols query is too long");
    error.with_context("max_bytes", std::to_string(kMaxLspProviderQueryBytes));
    return tool_error_result(call, error);
  }
  auto const tool_context = context_for_provider_tool(context, call);
  auto result = ava::tools::lsp_workspace_symbols(tool_context, *query);
  if (!result)
    return lsp_error_result(call, result.error());

  std::string text = "{\"tool\":\"lsp_workspace_symbols\",\"ok\":true,\"query\":\"" + ava::core::json::escape(*query) + "\",\"symbols\":[";
  bool truncated = false;
  append_lsp_entries(text, result->symbols, [&](ava::lsp::Symbol const& symbol) { return lsp_symbol_entry_json(context, symbol); }, truncated);
  text += "],\"truncated\":" + json_bool(truncated) + ",\"total_symbols\":" + std::to_string(result->symbols.size()) + "}";
  return ToolDispatchResult{.call_id = call.id, .name = call.name, .success = true, .result_text = std::move(text)};
}

ToolDispatchResult lsp_definition_result(ava::tools::ToolContext const& context, ProviderToolCall const& call)
{
  auto path = required_safe_string_arg(call.arguments_json, "path", call.name);
  if (!path)
    return tool_error_result(call, path.error());
  if (path->size() > kMaxLspProviderPathBytes)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "lsp_definition path is too long");
    error.with_context("max_bytes", std::to_string(kMaxLspProviderPathBytes));
    return tool_error_result(call, error);
  }
  auto line = required_nonnegative_int_arg(call.arguments_json, "line", call.name);
  if (!line)
    return tool_error_result(call, line.error());
  auto column = required_nonnegative_int_arg(call.arguments_json, "column", call.name);
  if (!column)
    return tool_error_result(call, column.error());

  auto const tool_context = context_for_provider_tool(context, call);
  auto result = ava::tools::lsp_definition(tool_context, workspace_path(context, *path), *line, *column);
  if (!result)
    return lsp_error_result(call, result.error());

  std::string text = "{\"tool\":\"lsp_definition\",\"ok\":true,\"path\":\"" + ava::core::json::escape(*path) + "\",\"line\":" + std::to_string(*line) +
                     ",\"column\":" + std::to_string(*column) + ",\"locations\":[";
  bool truncated = false;
  append_lsp_entries(text, result->locations, [&](ava::lsp::Location const& location) { return lsp_location_entry_json(context, location); }, truncated);
  text += "],\"truncated\":" + json_bool(truncated) + ",\"total_locations\":" + std::to_string(result->locations.size()) + "}";
  return ToolDispatchResult{.call_id = call.id, .name = call.name, .success = true, .result_text = std::move(text)};
}

ToolDispatchResult lsp_references_result(ava::tools::ToolContext const& context, ProviderToolCall const& call)
{
  auto path = required_safe_string_arg(call.arguments_json, "path", call.name);
  if (!path)
    return tool_error_result(call, path.error());
  if (path->size() > kMaxLspProviderPathBytes)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "lsp_references path is too long");
    error.with_context("max_bytes", std::to_string(kMaxLspProviderPathBytes));
    return tool_error_result(call, error);
  }
  auto line = required_nonnegative_int_arg(call.arguments_json, "line", call.name);
  if (!line)
    return tool_error_result(call, line.error());
  auto column = required_nonnegative_int_arg(call.arguments_json, "column", call.name);
  if (!column)
    return tool_error_result(call, column.error());

  auto const tool_context = context_for_provider_tool(context, call);
  auto result = ava::tools::lsp_references(tool_context, workspace_path(context, *path), *line, *column);
  if (!result)
    return lsp_error_result(call, result.error());

  std::string text = "{\"tool\":\"lsp_references\",\"ok\":true,\"path\":\"" + ava::core::json::escape(*path) + "\",\"line\":" + std::to_string(*line) +
                     ",\"column\":" + std::to_string(*column) + ",\"locations\":[";
  bool truncated = false;
  append_lsp_entries(text, result->locations, [&](ava::lsp::Location const& location) { return lsp_location_entry_json(context, location); }, truncated);
  text += "],\"truncated\":" + json_bool(truncated) + ",\"total_locations\":" + std::to_string(result->locations.size()) + "}";
  return ToolDispatchResult{.call_id = call.id, .name = call.name, .success = true, .result_text = std::move(text)};
}

ToolDispatchResult question_result(ava::tools::ToolContext const& context, ProviderToolCall const& call)
{
  auto prompt = parse_question_prompt(call.arguments_json, call.name);
  if (!prompt)
    return tool_error_result(call, prompt.error());
  if (!context.question_resolver)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::Tool, "question resolver is unavailable");
    error.with_context("tool", call.name);
    return tool_error_result(call, error);
  }
  auto answer = context.question_resolver(*prompt);
  if (!answer)
    return tool_error_result(call, answer.error());
  if (auto valid_answer = validate_question_answer(*answer, call.name); !valid_answer)
  {
    return tool_error_result(call, valid_answer.error());
  }
  return ToolDispatchResult{.call_id = call.id, .name = call.name, .success = true, .result_text = serialize_question_answer_result(*prompt, *answer)};
}

ToolExecutor builtin_tool_executor(std::string_view name)
{
  if (name == "read_file")
    return read_file_result;
  if (name == "write_file")
    return write_file_result;
  if (name == "edit_file")
    return edit_file_result;
  if (name == "glob")
    return glob_result;
  if (name == "list_directory")
    return list_directory_result;
  if (name == "grep")
    return grep_result;
  if (name == "bash")
    return bash_result;
  if (name == "webfetch")
    return webfetch_result;
  if (name == "websearch")
    return websearch_result;
  if (name == "skill")
    return skill_result;
  if (name == "task")
    return task_result;
  if (name == "lsp_diagnostics")
    return lsp_diagnostics_result;
  if (name == "lsp_document_symbols")
    return lsp_document_symbols_result;
  if (name == "lsp_workspace_symbols")
    return lsp_workspace_symbols_result;
  if (name == "lsp_definition")
    return lsp_definition_result;
  if (name == "lsp_references")
    return lsp_references_result;
  if (name == "apply_patch")
    return apply_patch_result;
  if (name == "question")
    return question_result;
  return nullptr;
}

ava::core::Result<ToolRegistry> build_tool_registry_result(ava::tools::ToolContext const& context)
{
  ToolRegistry registry;
  if (context.exact_builtin_tool_names)
  {
    if (!context.session_mcp_config)
      return std::unexpected(
          ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "exact tool composition requires an immutable session MCP configuration"));
    for (auto const& name : *context.exact_builtin_tool_names)
    {
      auto const* entry = builtin_tool_registry().find(name);
      if (entry == nullptr)
      {
        auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "requested built-in tool is unavailable");
        error.with_context("tool", name);
        return std::unexpected(std::move(error));
      }
      if (auto registered = registry.register_tool(*entry); !registered)
        return std::unexpected(std::move(registered.error()));
    }
    if (auto registered = ava::mcp::register_enabled_mcp_tools(registry, context); !registered)
      return std::unexpected(std::move(registered.error()));
    return registry;
  }

  for (auto const& entry : builtin_tool_registry().entries())
  {
    auto registered = registry.register_tool(entry);
    if (!registered)
    {
      std::cerr << "tool registry failed: " << registered.error().format() << '\n';
      std::abort();
    }
  }
  ava::plugin::register_enabled_plugin_tools(registry, context);
  if (auto registered = ava::mcp::register_enabled_mcp_tools(registry, context); !registered)
    return std::unexpected(std::move(registered.error()));
  registry.apply_visibility_filter(context);
  return registry;
}

ToolRegistry build_tool_registry(ava::tools::ToolContext const& context)
{
  auto registry = build_tool_registry_result(context);
  return registry ? std::move(*registry) : ToolRegistry{};
}

}  // namespace

ToolRegistry const& builtin_tool_registry()
{
  static auto const registry = [] {
    ToolRegistry builtins;
    for (auto const& metadata : builtin_tool_metadata())
    {
      auto registered = builtins.register_tool(RegisteredTool{.metadata = own_tool_metadata(metadata),
                                                              .executor = builtin_tool_executor(metadata.name),
                                                              .source = ToolSource::Builtin,
                                                              .source_id = "builtin",
                                                              .requires_lsp_diagnostics = is_lsp_diagnostics_metadata(metadata)});
      if (!registered)
      {
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
  if (!context.mutation_queue)
    context.mutation_queue = std::make_shared<ava::tools::MutationQueue>();
  context_ = std::move(context);
  registry_ = build_tool_registry(context_);
}

ToolDispatcher::ToolDispatcher(ava::tools::ToolContext context, ToolRegistry registry) : context_(std::move(context)), registry_(std::move(registry))
{
  if (!context_.mutation_queue)
    context_.mutation_queue = std::make_shared<ava::tools::MutationQueue>();
}

ava::core::Result<ToolDispatcher> ToolDispatcher::create_strict(ava::tools::ToolContext context)
{
  if (context.require_descriptor_secure_workspace && !context.secure_workspace)
  {
    auto workspace = ava::tools::SecureWorkspace::open(context.workspace_dir);
    if (!workspace)
      return std::unexpected(std::move(workspace.error()));
    context.secure_workspace = std::move(*workspace);
  }
  auto registry = build_tool_registry_result(context);
  if (!registry)
    return std::unexpected(std::move(registry.error()));
  return ToolDispatcher(std::move(context), std::move(*registry));
}

ava::core::Result<ToolDispatchResult> ToolDispatcher::dispatch(ProviderToolCall const& call) const
{
  return dispatch_with_context(context_, call);
}

ava::core::Result<ToolDispatchResult> ToolDispatcher::dispatch_with_context(ava::tools::ToolContext context, ProviderToolCall const& call) const
{
  auto const arguments = call.arguments_json.empty() ? std::string("{}") : call.arguments_json;
  ProviderToolCall const normalized{.id = call.id, .name = call.name, .arguments_json = arguments};
  if (normalized.id.empty())
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "tool call id is required");
    error.with_context("tool", normalized.name);
    return std::unexpected(std::move(error));
  }
  if (normalized.id.size() > kMaxProviderToolCallIdBytes)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "tool call id is too long");
    error.with_context("tool", normalized.name);
    error.with_context("max_bytes", std::to_string(kMaxProviderToolCallIdBytes));
    return std::unexpected(std::move(error));
  }
  if (auto safe_id = reject_control_value(normalized.id, "id", "tool call id contains a forbidden control byte"); !safe_id)
  {
    safe_id.error().with_context("tool", normalized.name);
    return std::unexpected(std::move(safe_id.error()));
  }
  auto const* tool = registry_.find(normalized.name);
  // Resolve before observing so only registry-owned canonical tool metadata can
  // cross the trace boundary. Provider-supplied names and call IDs remain
  // content/product data and are never used as trace correlation.
  auto const trace_call_id = context.observation ? context.observation->next_id("tool") : std::string{};
  auto emit_start = [&](std::string_view canonical_name, bool resolved) {
    if (!context.observation)
      return;
    context.observation->emit(
        ava::observability::TraceEventType::ToolDispatchStart, context.trace_context, [&normalized, &trace_call_id, canonical_name, resolved](auto& event) {
          event.call_id = trace_call_id;
          event.phase = ava::observability::TracePhase::Tool;
          event.outcome = ava::observability::TraceOutcome::Started;
          event.fields = {{.key = "tool_name",
                           .value = resolved ? std::string(canonical_name) : "[omitted]",
                           .provenance = resolved ? ava::observability::FieldProvenance::PublicMetadata : ava::observability::FieldProvenance::Content},
                          {.key = "arguments_bytes", .value = std::to_string(normalized.arguments_json.size())}};
        });
  };
  auto emit_result = [&](ToolDispatchResult const& result) {
    if (!context.observation)
      return;
    context.observation->emit(ava::observability::TraceEventType::ToolDispatchResult, context.trace_context, [&result, &trace_call_id](auto& event) {
      event.call_id = trace_call_id;
      event.phase = ava::observability::TracePhase::Tool;
      event.outcome = result.success ? ava::observability::TraceOutcome::Success
                                     : (result.payload.status == ToolResultStatus::Canceled ? ava::observability::TraceOutcome::Canceled
                                                                                            : ava::observability::TraceOutcome::Error);
      event.fields = {{.key = "result_bytes", .value = std::to_string(result.result_text.size())}};
    });
  };
  if (tool == nullptr)
  {
    emit_start({}, false);
    auto result = with_tool_result_payload(simple_error_result(normalized, ava::core::ErrorCategory::Tool, "unknown tool"));
    emit_result(result);
    return result;
  }

  emit_start(tool->metadata.name, true);
  context.trace_call_id = trace_call_id;
  if (is_canceled(context))
  {
    auto result = with_tool_result_payload(tool_error_result(normalized, canceled_error(normalized)));
    emit_result(result);
    return result;
  }
  context.permission_request_ids = std::make_shared<std::vector<std::string>>();
  if (context.announce_execution_after_permission)
    context.execution_started = std::make_shared<std::atomic_bool>(false);
  if (context.lsp_diagnostics_provider)
    context.lsp_diagnostics_provider->set_permission_request_ids(context.permission_request_ids);
  auto result = tool->executor(context, normalized);
  if (context.permission_request_ids && !context.permission_request_ids->empty())
  {
    result.payload.permission_request_ids = *context.permission_request_ids;
  }
  result = with_tool_result_payload(std::move(result));
  emit_result(result);
  return result;
}

std::vector<ToolMetadata> ToolDispatcher::registered_tool_metadata() const
{
  return registry_.metadata();
}

std::vector<std::string> ToolDispatcher::registered_tool_schemas_json() const
{
  return registry_.tool_schemas_json(context_);
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
