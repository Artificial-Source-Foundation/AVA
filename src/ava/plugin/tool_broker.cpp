#include "ava/plugin/tool_broker.h"

#include "ava/plugin/diagnostics.h"
#include "ava/plugin/discovery.h"
#include "ava/plugin/enablement.h"
#include "ava/plugin/runner.h"

#include "ava/permissions/permission.h"

#include "ava/core/error.h"
#include "ava/core/json.h"

#include "ava/tools/search_tools.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace ava::plugin {
namespace {

struct PluginToolBinding {
  PluginManifest manifest;
  PluginToolContribution contribution;
  std::string model_tool_name;
};

std::string json_bool(bool value)
{
  return value ? "true" : "false";
}

std::string error_json(std::string_view tool, ava::core::Error const& error)
{
  return "{\"tool\":\"" + ava::core::json::escape(tool) + "\",\"ok\":false,\"error\":{\"category\":\"" +
         ava::core::json::escape(ava::core::to_string(error.category())) + "\",\"message\":\"" +
         ava::core::json::escape(error.message()) + "\",\"details\":\"" + ava::core::json::escape(error.format()) +
         "\"}}";
}

bool is_canceled_error(ava::core::Error const& error)
{
  for (auto const& context : error.context()) {
    if (context.key == "canceled" && context.value == "true") return true;
  }
  return error.message() == "tool canceled";
}

bool is_canceled(ava::tools::ToolContext const& context)
{
  return context.cancel_requested && context.cancel_requested();
}

ava::core::VoidResult reject_control_value(std::string_view value, std::string_view field,
                                           std::string_view operation)
{
  for (char const ch : value) {
    auto const byte = static_cast<unsigned char>(ch);
    if (byte < 0x20 || byte == 0x7F) {
      auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument,
                                    "proxy argument contains a forbidden control byte");
      error.with_context("operation", std::string(operation));
      error.with_context("argument", std::string(field));
      return std::unexpected(std::move(error));
    }
  }
  return {};
}

ava::core::VoidResult reject_nul_value(std::string_view value, std::string_view field,
                                       std::string_view operation)
{
  if (value.find('\0') == std::string_view::npos) return {};
  auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument,
                                "proxy argument contains a forbidden NUL byte");
  error.with_context("operation", std::string(operation));
  error.with_context("argument", std::string(field));
  return std::unexpected(std::move(error));
}

ava::core::Result<std::optional<std::string>> optional_proxy_string_arg(std::string_view arguments,
                                                                        std::string_view field,
                                                                        std::string_view operation,
                                                                        bool allow_text_controls)
{
  if (!ava::core::json::field_value_start(arguments, field)) return std::optional<std::string>{};
  auto value = ava::core::json::string_field(arguments, field);
  if (!value) {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "proxy argument must be a string");
    error.with_context("operation", std::string(operation));
    error.with_context("argument", std::string(field));
    return std::unexpected(std::move(error));
  }
  auto safe = allow_text_controls ? reject_nul_value(*value, field, operation)
                                  : reject_control_value(*value, field, operation);
  if (!safe) return std::unexpected(std::move(safe.error()));
  return std::optional<std::string>{std::move(*value)};
}

ava::core::Result<std::string> required_proxy_string_arg(std::string_view arguments, std::string_view field,
                                                          std::string_view operation,
                                                          bool allow_text_controls = false)
{
  auto value = optional_proxy_string_arg(arguments, field, operation, allow_text_controls);
  if (!value) return std::unexpected(std::move(value.error()));
  if (*value) return std::move(**value);
  auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "proxy argument is required");
  error.with_context("operation", std::string(operation));
  error.with_context("argument", std::string(field));
  return std::unexpected(std::move(error));
}

bool json_object_has_no_fields(std::string_view json)
{
  auto index = std::size_t{0};
  while (index < json.size() && std::isspace(static_cast<unsigned char>(json[index]))) ++index;
  if (index >= json.size() || json[index] != '{') return false;
  ++index;
  while (index < json.size() && std::isspace(static_cast<unsigned char>(json[index]))) ++index;
  return index < json.size() && json[index] == '}';
}

ava::core::Result<std::size_t> proxy_size_arg(std::string_view arguments, std::string_view field,
                                                std::size_t fallback, std::size_t maximum,
                                                std::string_view operation)
{
  auto const start = ava::core::json::field_value_start(arguments, field);
  if (!start) return fallback;
  auto index = *start;
  if (index >= arguments.size() || !std::isdigit(static_cast<unsigned char>(arguments[index]))) {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument,
                                  "proxy argument must be a positive integer");
    error.with_context("operation", std::string(operation));
    error.with_context("argument", std::string(field));
    return std::unexpected(std::move(error));
  }
  std::size_t converted = 0;
  while (index < arguments.size() && std::isdigit(static_cast<unsigned char>(arguments[index]))) {
    auto const digit = static_cast<std::size_t>(arguments[index] - '0');
    if (converted <= maximum) converted = converted * 10 + digit;
    ++index;
  }
  auto const is_value_boundary = index >= arguments.size() || std::isspace(static_cast<unsigned char>(arguments[index])) != 0 ||
                                 arguments[index] == ',' || arguments[index] == '}' || arguments[index] == ']';
  if (!is_value_boundary || converted == 0) {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument,
                                  "proxy argument must be a positive integer");
    error.with_context("operation", std::string(operation));
    error.with_context("argument", std::string(field));
    return std::unexpected(std::move(error));
  }
  if (converted > maximum) return maximum;
  return converted;
}

ava::core::Result<bool> optional_proxy_bool_arg(std::string_view arguments, std::string_view field, bool fallback,
                                                std::string_view operation)
{
  auto const start = ava::core::json::field_value_start(arguments, field);
  if (!start) return fallback;
  auto const is_value_boundary = [&arguments](std::size_t index) {
    return index >= arguments.size() || std::isspace(static_cast<unsigned char>(arguments[index])) != 0 ||
           arguments[index] == ',' || arguments[index] == '}' || arguments[index] == ']';
  };
  if (arguments.substr(*start, 4) == "true" && is_value_boundary(*start + 4)) return true;
  if (arguments.substr(*start, 5) == "false" && is_value_boundary(*start + 5)) return false;
  auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "proxy argument must be a boolean");
  error.with_context("operation", std::string(operation));
  error.with_context("argument", std::string(field));
  return std::unexpected(std::move(error));
}

std::filesystem::path proxy_workspace_path(ava::tools::ToolContext const& context, std::string_view path)
{
  std::filesystem::path const parsed(path);
  if (parsed.is_absolute()) return parsed;
  return context.workspace_dir / parsed;
}

bool path_is_within(std::filesystem::path const& base, std::filesystem::path const& target)
{
  if (target == base) return true;
  std::error_code relative_error;
  auto const relative = std::filesystem::relative(target, base, relative_error);
  if (relative_error || relative.empty()) return false;
  return *relative.begin() != "..";
}

ava::core::Result<std::filesystem::path> resolve_proxy_path(ava::tools::ToolContext const& context,
                                                            std::string_view requested_path,
                                                            std::string_view operation)
{
  auto raw_path = proxy_workspace_path(context, requested_path).lexically_normal();
  std::error_code canonical_error;
  auto const canonical_path = std::filesystem::weakly_canonical(raw_path, canonical_error);
  if (canonical_error) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to resolve proxy path");
    error.with_context("operation", std::string(operation));
    error.with_context("path", raw_path.string());
    error.with_context("cause", canonical_error.message());
    return std::unexpected(std::move(error));
  }

  std::filesystem::path const parsed(requested_path);
  if (!parsed.is_absolute()) {
    std::error_code workspace_error;
    auto const workspace = std::filesystem::weakly_canonical(context.workspace_dir, workspace_error);
    auto const workspace_path = workspace_error ? std::filesystem::absolute(context.workspace_dir).lexically_normal()
                                                : workspace;
    if (!path_is_within(workspace_path, canonical_path)) {
      auto error = ava::core::Error(ava::core::ErrorCategory::PermissionDenied,
                                    "relative proxy paths must stay inside the workspace");
      error.with_context("operation", std::string(operation));
      error.with_context("path", std::string(requested_path));
      return std::unexpected(std::move(error));
    }
  }
  return canonical_path;
}

ava::core::VoidResult reject_proxy_non_regular_path(std::filesystem::path const& path, std::string_view operation)
{
  std::error_code status_error;
  auto const status = std::filesystem::symlink_status(path, status_error);
  if (status_error) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to inspect proxy path");
    error.with_context("operation", std::string(operation));
    error.with_context("path", path.string());
    error.with_context("cause", status_error.message());
    return std::unexpected(std::move(error));
  }
  if (!std::filesystem::is_regular_file(status)) {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument,
                                  "proxy path must be a regular file");
    error.with_context("operation", std::string(operation));
    error.with_context("path", path.string());
    return std::unexpected(std::move(error));
  }
  return {};
}

std::string proxy_bool(bool value)
{
  return value ? "true" : "false";
}

void append_proxy_spill_fields(std::string& text, std::filesystem::path const& path, bool spill_truncated)
{
  if (path.empty()) return;
  text += ",\"spill_file\":\"" + ava::core::json::escape(path.filename().generic_string()) + "\"";
  text += ",\"spill_truncated\":" + proxy_bool(spill_truncated);
}

PluginProxyResponse proxy_error_response(ava::core::Error const& error)
{
  return PluginProxyResponse{.ok = false,
                             .content = "",
                             .metadata_json = "",
                             .error_category = ava::core::to_string(error.category()),
                             .error_message = error.message(),
                             .error_details = error.format()};
}

ava::core::Result<PluginProxyResponse> proxy_result_or_error(ava::core::Error const& error)
{
  if (is_canceled_error(error)) return std::unexpected(error);
  return proxy_error_response(error);
}

std::string proxy_metadata_json(PluginManifest const& manifest, std::string_view contribution_kind,
                                std::string_view contribution_name, std::string_view model_operation_name,
                                std::string_view call_id, std::string_view operation)
{
  return "{\"operation\":\"" + ava::core::json::escape(operation) + "\",\"plugin_id\":\"" +
         ava::core::json::escape(manifest.id) + "\",\"contribution_kind\":\"" +
         ava::core::json::escape(contribution_kind) + "\",\"contribution_name\":\"" +
         ava::core::json::escape(contribution_name) + "\",\"model_operation\":\"" +
         ava::core::json::escape(model_operation_name) + "\",\"call_id\":\"" +
         ava::core::json::escape(call_id) + "\"}";
}

std::string read_proxy_content_json(std::string_view requested_path, ava::tools::TextOutput const& result)
{
  std::string text = "{\"operation\":\"file.read\",\"ok\":true,\"path\":\"" +
                     ava::core::json::escape(requested_path) + "\",\"content\":\"" +
                     ava::core::json::escape(result.content) + "\",\"truncated\":" + proxy_bool(result.truncated) +
                     ",\"byte_limited\":" + proxy_bool(result.byte_limited) + ",\"line_limited\":" +
                     proxy_bool(result.line_limited) + ",\"total_bytes\":" + std::to_string(result.total_bytes) +
                     ",\"output_bytes\":" + std::to_string(result.output_bytes) + ",\"output_lines\":" +
                     std::to_string(result.output_lines) + ",\"start_line\":" + std::to_string(result.start_line) +
                     ",\"end_line\":" + std::to_string(result.end_line) + ",\"total_lines\":" +
                     std::to_string(result.total_lines);
  if (result.next_offset_line > 0) {
    text += ",\"next_offset\":" + std::to_string(result.next_offset_line);
    text += ",\"next_offset_line\":" + std::to_string(result.next_offset_line);
  }
  text += '}';
  return text;
}

std::string glob_proxy_content_json(std::string_view pattern, ava::tools::GlobResult const& result)
{
  std::string text = "{\"operation\":\"file.search\",\"kind\":\"glob\",\"ok\":true,\"pattern\":\"" +
                     ava::core::json::escape(pattern) + "\",\"paths\":[";
  for (std::size_t index = 0; index < result.paths.size(); ++index) {
    if (index > 0) text += ',';
    text += "\"" + ava::core::json::escape(result.paths[index].generic_string()) + "\"";
  }
  text += "],\"truncated\":" + proxy_bool(result.truncated) + ",\"total_matches\":" +
          std::to_string(result.total_matches);
  append_proxy_spill_fields(text, result.spill_path, result.spill_truncated);
  text += '}';
  return text;
}

std::string grep_proxy_content_json(std::string_view query, std::string_view include,
                                    ava::tools::GrepResult const& result)
{
  std::string text = "{\"operation\":\"file.search\",\"kind\":\"grep\",\"ok\":true,\"query\":\"" +
                     ava::core::json::escape(query) + "\",\"include\":\"" + ava::core::json::escape(include) +
                     "\",\"matches\":[";
  for (std::size_t index = 0; index < result.matches.size(); ++index) {
    auto const& match = result.matches[index];
    if (index > 0) text += ',';
    text += "{\"path\":\"" + ava::core::json::escape(match.path.generic_string()) +
            "\",\"line_number\":" + std::to_string(match.line_number) + ",\"line\":\"" +
            ava::core::json::escape(match.line) + "\",\"line_truncated\":" + proxy_bool(match.line_truncated) +
            "}";
  }
  text += "],\"truncated\":" + proxy_bool(result.truncated) + ",\"total_matches\":" +
          std::to_string(result.total_matches);
  append_proxy_spill_fields(text, result.spill_path, result.spill_truncated);
  text += '}';
  return text;
}

std::string session_status_proxy_content_json(ava::tools::ToolContext const& context)
{
  auto const current_dir = context.current_dir.empty() ? context.workspace_dir : context.current_dir;
  return "{\"operation\":\"session.status\",\"ok\":true,\"session_id\":\"" +
         ava::core::json::escape(context.session_id) + "\",\"mode\":\"" +
         ava::core::json::escape(ava::agent::to_string(context.mode)) + "\",\"provider_id\":\"" +
         ava::core::json::escape(context.provider_id) + "\",\"model_id\":\"" +
         ava::core::json::escape(context.model_id) + "\",\"workspace\":\"" +
         ava::core::json::escape(context.workspace_dir.generic_string()) + "\",\"current_dir\":\"" +
         ava::core::json::escape(current_dir.generic_string()) + "\"}";
}

ava::agent::ToolDispatchResult tool_error_result(ava::agent::ProviderToolCall const& call,
                                                 ava::core::Error const& error)
{
  return ava::agent::ToolDispatchResult{.call_id = call.id,
                                        .name = call.name,
                                        .success = false,
                                        .result_text = error_json(call.name, error),
                                        .payload = [&] {
                                          ava::agent::ToolResultPayload payload;
                                          if (is_canceled_error(error)) {
                                            payload.status = ava::agent::ToolResultStatus::Canceled;
                                          }
                                          return payload;
                                        }()};
}

std::string result_json(ava::agent::ProviderToolCall const& call, PluginToolBinding const& binding,
                        PluginToolCallResult const& result)
{
  std::string text = "{\"tool\":\"" + ava::core::json::escape(call.name) + "\",\"ok\":" + json_bool(result.ok) +
                     ",\"plugin\":\"" + ava::core::json::escape(binding.manifest.id) + "\",\"plugin_tool\":\"" +
                     ava::core::json::escape(binding.contribution.name) + "\",\"content\":\"" +
                     ava::core::json::escape(result.content) + "\"";
  if (!result.metadata_json.empty()) text += ",\"metadata\":" + result.metadata_json;
  if (!result.ok) {
    text += ",\"error\":{\"category\":\"tool\",\"message\":\"" +
            ava::core::json::escape(result.content.empty() ? "plugin tool returned error" : result.content) + "\"";
    if (!result.metadata_json.empty()) text += ",\"details\":\"" + ava::core::json::escape(result.metadata_json) + "\"";
    text += '}';
  }
  text += '}';
  return text;
}

ava::agent::ToolResultPayload result_payload(PluginToolCallResult const& result, std::string const& text)
{
  ava::agent::ToolResultPayload payload;
  payload.status = result.ok ? ava::agent::ToolResultStatus::Success : ava::agent::ToolResultStatus::Error;
  payload.content_type = "application/json";
  payload.content = text;
  if (!result.ok) {
    payload.error_category = "tool";
    payload.error_message = result.content.empty() ? "plugin tool returned error" : result.content;
    payload.error_details = result.metadata_json;
  }
  return payload;
}

ava::core::Error plugin_tool_error(ava::core::ErrorCategory category, std::string message,
                                    PluginToolBinding const& binding)
{
  auto error = ava::core::Error(category, std::move(message));
  error.with_context("plugin", binding.manifest.id);
  error.with_context("plugin_tool", binding.contribution.name);
  error.with_context("tool", binding.model_tool_name);
  if (!binding.manifest.path.empty()) error.with_context("manifest", binding.manifest.path.string());
  return error;
}

ava::core::Result<PluginProxyResponse> dispatch_read_proxy(ava::tools::ToolContext const& context,
                                                           PluginProxyRequest const& request,
                                                           PluginManifest const& manifest,
                                                           std::string_view contribution_kind,
                                                           std::string_view contribution_name,
                                                           std::string_view model_operation_name,
                                                           std::string_view call_id)
{
  constexpr std::size_t kProxyReadMaxBytes = 8 * 1024;
  constexpr std::size_t kProxyReadMaxLines = 400;

  auto path = required_proxy_string_arg(request.arguments_json, "path", request.operation);
  if (!path) return proxy_result_or_error(path.error());
  if (path->empty()) {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument,
                                  "proxy argument must not be empty");
    error.with_context("operation", request.operation);
    error.with_context("argument", "path");
    return proxy_result_or_error(error);
  }
  auto resolved_path = resolve_proxy_path(context, *path, request.operation);
  if (!resolved_path) return proxy_result_or_error(resolved_path.error());
  if (auto regular = reject_proxy_non_regular_path(*resolved_path, request.operation); !regular) {
    return proxy_result_or_error(regular.error());
  }
  auto max_bytes = proxy_size_arg(request.arguments_json, "max_bytes", kProxyReadMaxBytes, kProxyReadMaxBytes,
                                  request.operation);
  if (!max_bytes) return proxy_result_or_error(max_bytes.error());
  auto offset = proxy_size_arg(request.arguments_json, "offset", 1, 100000000, request.operation);
  if (!offset) return proxy_result_or_error(offset.error());
  auto max_lines = proxy_size_arg(request.arguments_json, "limit", kProxyReadMaxLines, kProxyReadMaxLines,
                                  request.operation);
  if (!max_lines) return proxy_result_or_error(max_lines.error());

  auto result = ava::tools::read_file(
      context, *resolved_path,
      ava::tools::ReadOptions{.max_bytes = *max_bytes, .offset_line = *offset, .max_lines = *max_lines});
  if (!result) return proxy_result_or_error(result.error());
  return PluginProxyResponse{.ok = true,
                             .content = read_proxy_content_json(*path, *result),
                             .metadata_json = proxy_metadata_json(manifest, contribution_kind, contribution_name,
                                                                  model_operation_name, call_id, request.operation),
                             .error_category = "",
                             .error_message = "",
                             .error_details = ""};
}

ava::core::Result<PluginProxyResponse> dispatch_search_proxy(ava::tools::ToolContext const& context,
                                                             PluginProxyRequest const& request,
                                                             PluginManifest const& manifest,
                                                             std::string_view contribution_kind,
                                                             std::string_view contribution_name,
                                                             std::string_view model_operation_name,
                                                             std::string_view call_id)
{
  constexpr std::size_t kProxyGlobMaxResults = 128;
  constexpr std::size_t kProxyGrepMaxMatches = 64;
  constexpr std::size_t kProxyGrepMaxLineLength = 240;

  auto query = optional_proxy_string_arg(request.arguments_json, "query", request.operation, true);
  if (!query) return proxy_result_or_error(query.error());
  if (ava::core::json::field_value_start(request.arguments_json, "root")) {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument,
                                  "file.search root is not supported in this proxy version");
    error.with_context("operation", request.operation);
    error.with_context("argument", "root");
    return proxy_result_or_error(error);
  }
  auto pattern = optional_proxy_string_arg(request.arguments_json, "pattern", request.operation, false);
  if (!pattern) return proxy_result_or_error(pattern.error());
  auto include = optional_proxy_string_arg(request.arguments_json, "include", request.operation, false);
  if (!include) return proxy_result_or_error(include.error());
  auto literal = optional_proxy_bool_arg(request.arguments_json, "literal", true, request.operation);
  if (!literal) return proxy_result_or_error(literal.error());
  if (!*literal) {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument,
                                  "file.search regex mode is not supported in this proxy version");
    error.with_context("operation", request.operation);
    error.with_context("argument", "literal");
    return proxy_result_or_error(error);
  }
  auto case_insensitive = optional_proxy_bool_arg(request.arguments_json, "case_insensitive", false,
                                                  request.operation);
  if (!case_insensitive) return proxy_result_or_error(case_insensitive.error());

  if (query && *query) {
    auto const effective_include = (include && *include) ? **include : (pattern && *pattern ? **pattern : "**/*");
    auto max_matches = proxy_size_arg(request.arguments_json, "max_matches", kProxyGrepMaxMatches,
                                      kProxyGrepMaxMatches, request.operation);
    if (!max_matches) return proxy_result_or_error(max_matches.error());
    auto max_line_length = proxy_size_arg(request.arguments_json, "max_line_length", kProxyGrepMaxLineLength,
                                          kProxyGrepMaxLineLength, request.operation);
    if (!max_line_length) return proxy_result_or_error(max_line_length.error());
    auto result = ava::tools::grep_files(
        context, **query, effective_include,
        ava::tools::GrepOptions{.max_matches = *max_matches,
                                .max_line_length = *max_line_length,
                                .no_ignore = true,
                                .literal = *literal,
                                .case_insensitive = *case_insensitive,
                                .skip_symlinks = true});
    if (!result) return proxy_result_or_error(result.error());
    return PluginProxyResponse{.ok = true,
                               .content = grep_proxy_content_json(**query, effective_include, *result),
                               .metadata_json = proxy_metadata_json(manifest, contribution_kind, contribution_name,
                                                                    model_operation_name, call_id, request.operation),
                               .error_category = "",
                               .error_message = "",
                               .error_details = ""};
  }

  if (!pattern || !*pattern) {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument,
                                  "file.search requires query or pattern");
    error.with_context("operation", request.operation);
    return proxy_result_or_error(error);
  }
  auto max_results = proxy_size_arg(request.arguments_json, "max_results", kProxyGlobMaxResults,
                                    kProxyGlobMaxResults, request.operation);
  if (!max_results) return proxy_result_or_error(max_results.error());
  auto result = ava::tools::glob_files(context, **pattern, ava::tools::GlobOptions{.max_results = *max_results,
                                                                                   .no_ignore = true,
                                                                                   .skip_symlinks = true});
  if (!result) return proxy_result_or_error(result.error());
  return PluginProxyResponse{.ok = true,
                             .content = glob_proxy_content_json(**pattern, *result),
                             .metadata_json = proxy_metadata_json(manifest, contribution_kind, contribution_name,
                                                                  model_operation_name, call_id, request.operation),
                             .error_category = "",
                             .error_message = "",
                             .error_details = ""};
}

ava::core::Result<PluginProxyResponse> dispatch_session_status_proxy(ava::tools::ToolContext const& context,
                                                                     PluginProxyRequest const& request,
                                                                     PluginManifest const& manifest,
                                                                     std::string_view contribution_kind,
                                                                     std::string_view contribution_name,
                                                                      std::string_view model_operation_name,
                                                                      std::string_view call_id)
{
  if (!json_object_has_no_fields(request.arguments_json)) {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument,
                                  "session.status does not accept arguments");
    error.with_context("operation", request.operation);
    return proxy_result_or_error(error);
  }
  if (context.session_id.empty()) {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument,
                                  "session status is unavailable in this proxy context");
    error.with_context("operation", request.operation);
    return proxy_result_or_error(error);
  }
  return PluginProxyResponse{.ok = true,
                             .content = session_status_proxy_content_json(context),
                             .metadata_json = proxy_metadata_json(manifest, contribution_kind, contribution_name,
                                                                  model_operation_name, call_id, request.operation),
                             .error_category = "",
                             .error_message = "",
                             .error_details = ""};
}

ava::agent::ToolDispatchResult dispatch_plugin_tool(ava::tools::ToolContext const& context,
                                                    ava::agent::ProviderToolCall const& call,
                                                    PluginToolBinding const& binding)
{
  if (is_canceled(context)) {
    return tool_error_result(
        call, plugin_tool_error(ava::core::ErrorCategory::Unknown, "plugin tool call canceled", binding));
  }
  if (!ava::core::json::is_valid_object(call.arguments_json)) {
    auto error = plugin_tool_error(ava::core::ErrorCategory::InvalidArgument,
                                   "plugin tool arguments must be a JSON object", binding);
    return tool_error_result(call, error);
  }

  auto tool_context = context;
  tool_context.permission_tool_name = call.name;
  tool_context.current_tool_name = call.name;
  tool_context.current_call_id = call.id;
  auto const command = binding.manifest.id + ":" + binding.contribution.name;
  if (auto permission =
          ava::tools::ensure_permission(tool_context, ava::permissions::Operation::PluginExecute, binding.manifest.path,
                                        binding.manifest.id, call.name, "plugin process launch requires permission");
      !permission) {
    return tool_error_result(call, permission.error());
  }
  if (is_canceled(tool_context)) {
    return tool_error_result(
        call, plugin_tool_error(ava::core::ErrorCategory::Unknown, "plugin tool call canceled", binding));
  }
  if (auto permission = ava::tools::ensure_permission(tool_context, ava::permissions::Operation::PluginToolCall,
                                                      binding.manifest.path, command, call.name,
                                                      "plugin tool call requires permission");
      !permission) {
    return tool_error_result(call, permission.error());
  }
  if (is_canceled(tool_context)) {
    return tool_error_result(
        call, plugin_tool_error(ava::core::ErrorCategory::Unknown, "plugin tool call canceled", binding));
  }

  PluginRunnerOptions options;
  options.workspace_dir = context.workspace_dir;
  auto process = PluginProcess::start(binding.manifest, options, context.cancel_requested);
  if (!process) return tool_error_result(call, process.error());

  auto proxy_handler = make_core_service_proxy_handler(tool_context, binding.manifest, "tool",
                                                       binding.contribution.name, call.name, call.id);
  auto result = (*process)->call_tool(binding.contribution.name, call.arguments_json, call.id,
                                      context.cancel_requested, std::move(proxy_handler));
  auto shutdown = (*process)->shutdown();
  if (!result) return tool_error_result(call, result.error());
  if (!shutdown) return tool_error_result(call, shutdown.error());

  auto text = result_json(call, binding, *result);
  return ava::agent::ToolDispatchResult{.call_id = call.id,
                                        .name = call.name,
                                        .success = result->ok,
                                        .result_text = text,
                                        .payload = result_payload(*result, text)};
}

std::string schema_json(std::string_view model_tool_name, PluginToolContribution const& contribution)
{
  auto const description =
      contribution.description.empty() ? std::string("Plugin tool ") + contribution.name : contribution.description;
  return "{\"type\":\"function\",\"name\":\"" + ava::core::json::escape(model_tool_name) + "\",\"description\":\"" +
         ava::core::json::escape(description) + "\",\"parameters\":" + contribution.input_schema_json + '}';
}

ava::agent::RegisteredToolMetadata metadata_for_tool(std::string model_tool_name,
                                                     PluginToolContribution const& contribution)
{
  auto const description =
      contribution.description.empty() ? std::string("Plugin tool ") + contribution.name : contribution.description;
  auto const schema = schema_json(model_tool_name, contribution);
  return ava::agent::RegisteredToolMetadata{
      .name = std::move(model_tool_name),
      .description = description,
      .schema_json = schema,
      .permission_category = "plugin.tool.call",
      .output_bound_summary = "Plugin tool output is bounded by JSONL record size",
      .execution_mode = "plugin_process",
      .event_rendering_hint = "plugin_tool",
      .description_family = "plugin"};
}

PluginDiscoveryOptions discovery_options_for_context(ava::tools::ToolContext const& context)
{
  auto options = default_plugin_discovery_options(context.workspace_dir);
  if (!context.plugin_global_plugins_dir.empty()) options.global_plugins_dir = context.plugin_global_plugins_dir;
  if (!context.plugin_project_plugins_dir.empty()) options.project_plugins_dir = context.plugin_project_plugins_dir;
  return options;
}

std::filesystem::path enablement_file_for_context(ava::tools::ToolContext const& context)
{
  if (!context.plugin_enablement_file.empty()) return context.plugin_enablement_file;
  return default_plugin_enablement_file();
}

}  // namespace

std::string plugin_model_tool_name(std::string_view plugin_id, std::string_view tool_name)
{
  auto sanitize = [](std::string_view value) {
    std::string sanitized;
    sanitized.reserve(value.size());
    bool last_was_separator = false;
    for (char const ch : value) {
      auto const byte = static_cast<unsigned char>(ch);
      if (std::isalnum(byte) != 0) {
        sanitized.push_back(static_cast<char>(std::tolower(byte)));
        last_was_separator = false;
      } else if (!last_was_separator) {
        sanitized.push_back('_');
        last_was_separator = true;
      }
    }
    while (!sanitized.empty() && sanitized.back() == '_') sanitized.pop_back();
    if (sanitized.empty()) return std::string("tool");
    return sanitized;
  };
  return "plugin_" + sanitize(plugin_id) + "_" + sanitize(tool_name);
}

PluginProxyHandler make_core_service_proxy_handler(ava::tools::ToolContext context, PluginManifest manifest,
                                                   std::string contribution_kind, std::string contribution_name,
                                                   std::string model_operation_name, std::string call_id)
{
  return [context = std::move(context), manifest = std::move(manifest),
          contribution_kind = std::move(contribution_kind), contribution_name = std::move(contribution_name),
          model_operation_name = std::move(model_operation_name), call_id = std::move(call_id)](
    PluginProxyRequest const& request,
    CancelCallback proxy_cancel_requested) -> ava::core::Result<PluginProxyResponse> {
    auto proxy_context = context;
    auto original_cancel_requested = context.cancel_requested;
    proxy_context.cancel_requested = [original_cancel_requested, proxy_cancel_requested] {
      return (original_cancel_requested && original_cancel_requested()) ||
             (proxy_cancel_requested && proxy_cancel_requested());
    };
    proxy_context.permission_tool_name = model_operation_name + ":proxy:" + request.operation;
    proxy_context.permission_actor = "plugin:" + manifest.id + ":" + contribution_kind + ":" + contribution_name;
    proxy_context.current_tool_name = model_operation_name;
    proxy_context.current_call_id = call_id;

    if (request.operation == "file.read") {
      return dispatch_read_proxy(proxy_context, request, manifest, contribution_kind, contribution_name,
                                 model_operation_name, call_id);
    }
    if (request.operation == "file.search") {
      return dispatch_search_proxy(proxy_context, request, manifest, contribution_kind, contribution_name,
                                   model_operation_name, call_id);
    }
    if (request.operation == "session.status") {
      return dispatch_session_status_proxy(proxy_context, request, manifest, contribution_kind, contribution_name,
                                           model_operation_name, call_id);
    }
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument,
                                  "plugin proxy operation is not supported");
    error.with_context("operation", request.operation);
    error.with_context("plugin", manifest.id);
    return proxy_error_response(error);
  };
}

void register_enabled_plugin_tools(ava::agent::ToolRegistry& registry, ava::tools::ToolContext const& context)
{
  if (context.workspace_dir.empty()) return;

  auto diagnostics = collect_plugin_diagnostics(discovery_options_for_context(context),
                                                enablement_file_for_context(context), context.workspace_dir);
  for (auto const& status : diagnostics.plugins) {
    if (!status.enabled) continue;
    auto const& manifest = status.plugin.manifest;
    for (auto const& contribution : manifest.contributes.tools) {
      auto const model_tool_name = plugin_model_tool_name(manifest.id, contribution.name);
      if (registry.find(model_tool_name) != nullptr) continue;

      auto binding = std::make_shared<PluginToolBinding const>(
          PluginToolBinding{.manifest = manifest, .contribution = contribution, .model_tool_name = model_tool_name});
      auto registered = registry.register_tool(ava::agent::RegisteredTool{
          .metadata = metadata_for_tool(model_tool_name, contribution),
          .executor =
              [binding](ava::tools::ToolContext const& tool_context, ava::agent::ProviderToolCall const& call) {
                return dispatch_plugin_tool(tool_context, call, *binding);
              },
          .source = ava::agent::ToolSource::Plugin,
          .source_id = manifest.id,
          .brokered_external = true,
          .requires_lsp_diagnostics = false});
      (void)registered;
    }
  }
}

}  // namespace ava::plugin
