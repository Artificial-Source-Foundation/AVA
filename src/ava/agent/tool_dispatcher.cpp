#include "ava/agent/tool_dispatcher.h"

#include <filesystem>
#include <map>
#include <string_view>
#include <utility>

#include "ava/core/json.h"
#include "ava/tools/bash_tool.h"
#include "ava/tools/search_tools.h"

namespace ava::agent {
namespace {

std::string json_bool(bool value) { return value ? "true" : "false"; }

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

std::size_t optional_size_arg(std::string_view arguments, std::string_view field, std::size_t fallback,
                              std::size_t maximum) {
  const auto value = ava::core::json::integer_field(arguments, field);
  if (!value || *value <= 0) return fallback;
  const auto converted = static_cast<unsigned long long>(*value);
  if (converted > maximum) return maximum;
  return static_cast<std::size_t>(converted);
}

ToolDispatchResult read_file_result(const ava::tools::ToolContext& context, const ProviderToolCall& call) {
  auto path = required_safe_string_arg(call.arguments_json, "path", call.name);
  if (!path) return tool_error_result(call, path.error());
  auto result = ava::tools::read_file(
      context, workspace_path(context, *path),
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
  auto content = required_string_arg(call.arguments_json, "content", call.name);
  if (!content) return tool_error_result(call, content.error());
  auto result = ava::tools::write_file(context, workspace_path(context, *path), *content);
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
  auto old_text = required_string_arg(call.arguments_json, "old_text", call.name);
  if (!old_text) return tool_error_result(call, old_text.error());
  auto new_text = required_string_arg(call.arguments_json, "new_text", call.name);
  if (!new_text) return tool_error_result(call, new_text.error());
  auto result = ava::tools::edit_file(context, workspace_path(context, *path), *old_text, *new_text);
  if (!result) return tool_error_result(call, result.error());
  return ToolDispatchResult{.call_id = call.id,
                            .name = call.name,
                            .success = true,
                            .result_text = "{\"tool\":\"edit_file\",\"ok\":true,\"path\":\"" +
                                           ava::core::json::escape(result->path.generic_string()) +
                                           "\",\"bytes_written\":" + std::to_string(result->bytes_written) + "}"};
}

ToolDispatchResult glob_result(const ava::tools::ToolContext& context, const ProviderToolCall& call) {
  auto pattern = required_string_arg(call.arguments_json, "pattern", call.name);
  if (!pattern) return tool_error_result(call, pattern.error());
  auto result = ava::tools::glob_files(
      context, *pattern,
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
  auto pattern = required_string_arg(call.arguments_json, "pattern", call.name);
  if (!pattern) return tool_error_result(call, pattern.error());
  const auto include = ava::core::json::string_field(call.arguments_json, "include").value_or("**/*");
  auto result = ava::tools::grep_files(
      context, *pattern, include,
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
  auto result = ava::tools::run_bash(
      context, *command,
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

  std::map<std::filesystem::path, std::string> final_contents;
  std::vector<std::filesystem::path> applied_paths;
  for (std::size_t index = 0; index < edits.size(); ++index) {
    auto path = required_safe_string_arg(edits[index], "path", call.name);
    if (!path) return tool_error_result(call, path.error());
    auto old_text = required_string_arg(edits[index], "old_text", call.name);
    if (!old_text) return tool_error_result(call, old_text.error());
    auto new_text = required_string_arg(edits[index], "new_text", call.name);
    if (!new_text) return tool_error_result(call, new_text.error());

    const auto target = workspace_path(context, *path);
    const auto decision = ava::permissions::decide(ava::permissions::PermissionRequest{
        .operation = ava::permissions::Operation::EditFile,
        .mode = context.mode,
        .workspace_dir = context.workspace_dir,
        .target_path = target,
        .command = "",
    });
    if (decision.action != ava::permissions::PermissionAction::Allow) {
      auto error = ava::core::Error(ava::core::ErrorCategory::PermissionDenied, "patch edit requires permission");
      error.with_context("action", ava::permissions::to_string(decision.action));
      error.with_context("reason", decision.reason);
      error.with_context("path", target.string());
      return tool_error_result(call, error);
    }

    if (!final_contents.contains(target)) {
      auto content = ava::tools::read_file(context, target, ava::tools::ReadOptions{.max_bytes = 10 * 1024 * 1024});
      if (!content) return tool_error_result(call, content.error());
      if (content->truncated) {
        auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "file is too large for apply_patch");
        error.with_context("path", target.string());
        error.with_context("max_bytes", std::to_string(10 * 1024 * 1024));
        return tool_error_result(call, error);
      }
      final_contents.emplace(target, std::move(content->content));
      applied_paths.push_back(target);
    }

    auto& content = final_contents[target];
    const auto first = content.find(*old_text);
    if (first == std::string::npos) {
      auto error = ava::core::Error(ava::core::ErrorCategory::NotFound, "patch old_text was not found");
      error.with_context("path", target.string());
      return tool_error_result(call, error);
    }
    if (content.find(*old_text, first + old_text->size()) != std::string::npos) {
      auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "patch old_text is not unique");
      error.with_context("path", target.string());
      return tool_error_result(call, error);
    }
    content.replace(first, old_text->size(), *new_text);
  }

  std::string text = "{\"tool\":\"apply_patch\",\"ok\":true,\"edits\":[";
  for (std::size_t index = 0; index < applied_paths.size(); ++index) {
    const auto& path = applied_paths[index];
    auto result = ava::tools::write_file(context, path, final_contents[path]);
    if (!result) return tool_error_result(call, result.error());

    if (index > 0) text += ',';
    text += "{\"path\":\"" + ava::core::json::escape(result->path.generic_string()) +
            "\",\"bytes_written\":" + std::to_string(result->bytes_written) + "}";
  }
  text += "]}";
  return ToolDispatchResult{.call_id = call.id, .name = call.name, .success = true, .result_text = std::move(text)};
}

ToolDispatchResult question_result(const ProviderToolCall& call) {
  auto question = required_string_arg(call.arguments_json, "question", call.name);
  if (!question) return tool_error_result(call, question.error());
  return ToolDispatchResult{.call_id = call.id,
                            .name = call.name,
                            .success = true,
                            .result_text = "{\"tool\":\"question\",\"ok\":true,\"question\":\"" +
                                           ava::core::json::escape(*question) +
                                           "\",\"answer\":\"Interactive question prompts are not modal in AVA 0.1. "
                                           "Ask the user directly in the assistant response.\"}"};
}

}  // namespace

ToolDispatcher::ToolDispatcher(ava::tools::ToolContext context) : context_(std::move(context)) {}

ava::core::Result<ToolDispatchResult> ToolDispatcher::dispatch(const ProviderToolCall& call) const {
  const auto arguments = call.arguments_json.empty() ? std::string("{}") : call.arguments_json;
  const ProviderToolCall normalized{.id = call.id, .name = call.name, .arguments_json = arguments};
  if (normalized.name == "read_file") return read_file_result(context_, normalized);
  if (normalized.name == "write_file") return write_file_result(context_, normalized);
  if (normalized.name == "edit_file") return edit_file_result(context_, normalized);
  if (normalized.name == "glob") return glob_result(context_, normalized);
  if (normalized.name == "grep") return grep_result(context_, normalized);
  if (normalized.name == "bash") return bash_result(context_, normalized);
  if (normalized.name == "apply_patch") return apply_patch_result(context_, normalized);
  if (normalized.name == "question") return question_result(normalized);
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
      R"({"type":"function","name":"question","description":"Ask the user a clarification question. In AVA 0.1 this records the question and asks the assistant to surface it directly.","parameters":{"type":"object","properties":{"question":{"type":"string"}},"required":["question"]}})"};
}

}  // namespace ava::agent
