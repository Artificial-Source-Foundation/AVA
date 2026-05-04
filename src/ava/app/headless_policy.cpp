#include "ava/app/headless_policy.h"

#include <algorithm>
#include <set>
#include <string>
#include <utility>

#include "ava/core/error.h"

namespace ava::app {
namespace {

ava::core::Error unsupported_allow_error(std::string_view value)
{
  auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "unsupported --allow value");
  error.with_context("value", std::string(value));
  error.with_context("supported", "read-only");
  return error;
}

ava::core::Error unsupported_allow_tool_error(std::string_view value)
{
  auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "unsupported --allow-tool value");
  error.with_context("value", std::string(value));
  error.with_context("supported", "glob, grep, read_file, webfetch");
  return error;
}

bool is_supported_tool(std::string_view value)
{
  return value == "glob" || value == "grep" || value == "read_file" || value == "webfetch";
}

bool contains_tool(std::vector<std::string> const& tools, std::string_view value)
{
  return std::ranges::any_of(tools, [value](std::string const& tool) { return tool == value; });
}

bool prompt_matches_read_only(ava::permissions::PermissionPrompt const& prompt)
{
  return prompt.operation == ava::permissions::Operation::ReadFile ||
         prompt.operation == ava::permissions::Operation::SearchFiles;
}

bool prompt_matches_allowed_tool(ava::permissions::PermissionPrompt const& prompt, std::set<std::string> const& tools)
{
  if (prompt.tool_name == "read_file") {
    return prompt.operation == ava::permissions::Operation::ReadFile && tools.contains("read_file");
  }
  if (prompt.tool_name == "glob" || prompt.tool_name == "grep") {
    return prompt.operation == ava::permissions::Operation::SearchFiles && tools.contains(prompt.tool_name);
  }
  if (prompt.tool_name == "webfetch") {
    return prompt.operation == ava::permissions::Operation::NetworkFetch && tools.contains("webfetch");
  }
  return false;
}

}  // namespace

ava::core::VoidResult add_headless_allow_policy(HeadlessPermissionPolicyOptions& options, std::string_view value)
{
  if (value != "read-only") {
    return std::unexpected(unsupported_allow_error(value));
  }
  options.allow_read_only = true;
  return {};
}

ava::core::VoidResult add_headless_allowed_tools(HeadlessPermissionPolicyOptions& options, std::string_view value)
{
  std::size_t start = 0;
  while (start <= value.size()) {
    auto const comma = value.find(',', start);
    auto const end = comma == std::string_view::npos ? value.size() : comma;
    auto const tool = value.substr(start, end - start);
    if (tool.empty() || !is_supported_tool(tool)) {
      return std::unexpected(unsupported_allow_tool_error(tool));
    }
    if (!contains_tool(options.allowed_tools, tool)) {
      options.allowed_tools.emplace_back(tool);
    }
    if (comma == std::string_view::npos) {
      break;
    }
    start = comma + 1;
  }
  return {};
}

ava::permissions::PermissionResolver build_headless_permission_resolver(HeadlessPermissionPolicyOptions options)
{
  std::set<std::string> allowed_tools(options.allowed_tools.begin(), options.allowed_tools.end());
  return [allow_read_only = options.allow_read_only,
          allowed_tools = std::move(allowed_tools)](ava::permissions::PermissionPrompt const& prompt)
             -> ava::core::Result<ava::permissions::PermissionResolution> {
    if (allow_read_only && prompt_matches_read_only(prompt)) {
      return ava::permissions::PermissionResolution::Allow;
    }
    if (prompt_matches_allowed_tool(prompt, allowed_tools)) {
      return ava::permissions::PermissionResolution::Allow;
    }
    return ava::permissions::PermissionResolution::Deny;
  };
}

}  // namespace ava::app
