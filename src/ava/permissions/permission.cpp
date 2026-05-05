#include "ava/permissions/permission.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <utility>
#include <vector>

namespace ava::permissions {

namespace {

std::string lowercase(std::string_view value)
{
  std::string result(value);
  std::ranges::transform(result, result.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return result;
}

bool contains_any(std::string_view value, std::vector<std::string_view> const& needles)
{
  return std::ranges::any_of(needles,
                             [value](std::string_view needle) { return value.find(needle) != std::string_view::npos; });
}

bool equals_any(std::string_view value, std::vector<std::string_view> const& candidates)
{
  return std::ranges::any_of(candidates, [value](std::string_view candidate) { return value == candidate; });
}

bool is_within_workspace(std::filesystem::path const& workspace_dir, std::filesystem::path const& target_path)
{
  if (target_path.empty()) {
    return true;
  }

  std::error_code workspace_error;
  std::error_code target_error;
  auto const workspace_path = std::filesystem::weakly_canonical(workspace_dir, workspace_error);
  auto const target_path_normalized = std::filesystem::weakly_canonical(target_path, target_error);
  auto const workspace = workspace_error ? std::filesystem::absolute(workspace_dir).lexically_normal() : workspace_path;
  auto const target = target_error ? std::filesystem::absolute(target_path).lexically_normal() : target_path_normalized;
  if (target == workspace) {
    return true;
  }

  std::error_code relative_error;
  auto const relative = std::filesystem::relative(target, workspace, relative_error);
  if (relative_error || relative.empty()) {
    return false;
  }
  auto const first = *relative.begin();
  return first != "..";
}

std::filesystem::path policy_path(std::filesystem::path const& path)
{
  std::error_code error;
  auto const normalized = std::filesystem::weakly_canonical(path, error);
  if (!error) {
    return normalized;
  }
  return std::filesystem::absolute(path).lexically_normal();
}

bool is_secret_path(std::filesystem::path const& path)
{
  for (auto const& part : path.lexically_normal()) {
    auto const component = lowercase(part.string());
    if (equals_any(component, {".ssh", ".aws", ".gnupg", ".config/gcloud"})) {
      return true;
    }
  }

  auto const filename = lowercase(path.filename().string());
  if (filename == ".env" || filename.starts_with(".env.")) {
    return filename != ".env.example";
  }
  if (equals_any(filename, {".npmrc", ".netrc", ".pypirc", ".gem/credentials", ".dockerconfigjson", "credentials.json",
                            "auth.json", "config.json"})) {
    auto const full = lowercase(path.lexically_normal().string());
    return filename != "config.json" || contains_any(full, {"/.docker/", "\\.docker\\"});
  }

  auto const full = lowercase(path.string());
  return contains_any(full, {"id_rsa", "id_ed25519", "id_ecdsa", "id_dsa", "/.ssh/", "\\.ssh\\", "/.aws/", "\\.aws\\",
                             "/.gnupg/", "\\.gnupg\\", "credentials", "secret", "token"});
}

bool is_markdown_path(std::filesystem::path const& path)
{
  return lowercase(path.extension().string()) == ".md";
}

bool is_planning_markdown(std::filesystem::path const& path)
{
  auto const normalized = lowercase(path.lexically_normal().string());
  return is_markdown_path(path) &&
         (normalized.find("docs/") != std::string::npos || normalized.find("plan") != std::string::npos ||
          normalized.find("version") != std::string::npos);
}

PermissionDecision decision(PermissionAction action, std::string reason, PermissionRisk risk)
{
  return PermissionDecision{.action = action, .reason = std::move(reason), .risk = risk};
}

PermissionRisk default_allow_risk(Operation operation)
{
  switch (operation) {
    case Operation::EditFile:
    case Operation::RunCommand:
    case Operation::NetworkFetch:
    case Operation::PluginExecute:
    case Operation::PluginToolCall:
    case Operation::PluginCommandRun:
    case Operation::PluginEventObserve:
    case Operation::McpServerLaunch:
    case Operation::McpServerConnect:
    case Operation::McpToolCall:
      return PermissionRisk::Medium;
    case Operation::ReadFile:
    case Operation::SearchFiles:
    case Operation::LspQuery:
      return PermissionRisk::Low;
  }
  return PermissionRisk::Medium;
}

}  // namespace

PermissionDecision decide(PermissionRequest const& request)
{
  auto const checked_path = policy_path(request.target_path);

  if ((request.operation == Operation::ReadFile || request.operation == Operation::EditFile ||
       request.operation == Operation::LspQuery) &&
      is_secret_path(checked_path)) {
    return decision(PermissionAction::Deny, "target looks like a secret file", PermissionRisk::Critical);
  }

  if (!is_within_workspace(request.workspace_dir, request.target_path)) {
    return decision(PermissionAction::Ask, "target is outside the workspace", PermissionRisk::High);
  }

  if (request.operation == Operation::EditFile && request.mode == ava::agent::Mode::Plan &&
      !is_planning_markdown(checked_path)) {
    return decision(PermissionAction::Deny, "plan mode can only edit planning markdown", PermissionRisk::High);
  }

  if (request.operation == Operation::RunCommand) {
    return classify_command(request.command);
  }

  if (request.operation == Operation::NetworkFetch) {
    return decision(PermissionAction::Ask, "network fetch requires explicit approval", PermissionRisk::Medium);
  }

  if (request.operation == Operation::PluginExecute) {
    return decision(PermissionAction::Ask, "plugin subprocess execution requires explicit approval",
                    PermissionRisk::High);
  }

  if (request.operation == Operation::PluginToolCall) {
    return decision(PermissionAction::Ask, "plugin tool calls require explicit approval", PermissionRisk::High);
  }

  if (request.operation == Operation::PluginCommandRun) {
    return decision(PermissionAction::Ask, "plugin commands require explicit approval", PermissionRisk::High);
  }

  if (request.operation == Operation::PluginEventObserve) {
    return decision(PermissionAction::Ask, "plugin event observation requires explicit approval",
                    PermissionRisk::Medium);
  }

  if (request.operation == Operation::McpServerLaunch) {
    return decision(PermissionAction::Ask, "MCP server launch requires explicit approval", PermissionRisk::High);
  }

  if (request.operation == Operation::McpServerConnect) {
    return decision(PermissionAction::Ask, "MCP server connection requires explicit approval", PermissionRisk::High);
  }

  if (request.operation == Operation::McpToolCall) {
    return decision(PermissionAction::Ask, "MCP tool calls require explicit approval", PermissionRisk::High);
  }

  return decision(PermissionAction::Allow, "allowed by default workspace policy",
                  default_allow_risk(request.operation));
}

std::string to_string(PermissionAction action)
{
  switch (action) {
    case PermissionAction::Allow:
      return "allow";
    case PermissionAction::Ask:
      return "ask";
    case PermissionAction::Deny:
      return "deny";
  }
  return "deny";
}

std::string to_string(PermissionResolution resolution)
{
  switch (resolution) {
    case PermissionResolution::Allow:
    case PermissionResolution::AllowSessionGrant:
      return "allow";
    case PermissionResolution::Deny:
      return "deny";
  }
  return "deny";
}

std::string to_string(PermissionRisk risk)
{
  switch (risk) {
    case PermissionRisk::Low:
      return "low";
    case PermissionRisk::Medium:
      return "medium";
    case PermissionRisk::High:
      return "high";
    case PermissionRisk::Critical:
      return "critical";
  }
  return "high";
}

std::string to_string(Operation operation)
{
  switch (operation) {
    case Operation::ReadFile:
      return "read";
    case Operation::SearchFiles:
      return "search";
    case Operation::EditFile:
      return "edit";
    case Operation::RunCommand:
      return "bash";
    case Operation::NetworkFetch:
      return "network.fetch";
    case Operation::LspQuery:
      return "lsp.query";
    case Operation::PluginExecute:
      return "plugin.execute";
    case Operation::PluginToolCall:
      return "plugin.tool.call";
    case Operation::PluginCommandRun:
      return "plugin.command.run";
    case Operation::PluginEventObserve:
      return "plugin.event.observe";
    case Operation::McpServerLaunch:
      return "mcp.server.launch";
    case Operation::McpServerConnect:
      return "mcp.server.connect";
    case Operation::McpToolCall:
      return "mcp.tool.call";
  }
  return "unknown";
}

}  // namespace ava::permissions
