#include "sys.h"
#include "ava/permissions/permission.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <utility>
#include <vector>

namespace ava::permissions {

PermissionResolutionDecision::PermissionResolutionDecision(PermissionResolution resolution_in) : resolution(resolution_in)
{
}

PermissionResolutionDecision::PermissionResolutionDecision(PermissionResolution resolution_in, std::string reason_in)
    : resolution(resolution_in), reason(std::move(reason_in))
{
}

bool operator==(PermissionResolutionDecision const& decision, PermissionResolution resolution)
{
  return decision.resolution == resolution;
}

bool operator==(PermissionResolution resolution, PermissionResolutionDecision const& decision)
{
  return decision == resolution;
}

std::optional<PermissionAction> parse_permission_action(std::string_view value)
{
  if (value == "allow")
    return PermissionAction::Allow;
  if (value == "ask")
    return PermissionAction::Ask;
  if (value == "deny")
    return PermissionAction::Deny;
  return std::nullopt;
}

std::optional<Operation> parse_operation(std::string_view value)
{
  if (value == "read")
    return Operation::ReadFile;
  if (value == "search")
    return Operation::SearchFiles;
  if (value == "edit")
    return Operation::EditFile;
  if (value == "bash")
    return Operation::RunCommand;
  if (value == "network.fetch")
    return Operation::NetworkFetch;
  if (value == "network.search")
    return Operation::NetworkSearch;
  if (value == "lsp.server.launch")
    return Operation::LspServerLaunch;
  if (value == "lsp.query")
    return Operation::LspQuery;
  if (value == "skill")
    return Operation::SkillLoad;
  if (value == "task")
    return Operation::TaskRun;
  if (value == "plugin.execute")
    return Operation::PluginExecute;
  if (value == "plugin.tool.call")
    return Operation::PluginToolCall;
  if (value == "plugin.command.run")
    return Operation::PluginCommandRun;
  if (value == "plugin.event.observe")
    return Operation::PluginEventObserve;
  if (value == "mcp.server.launch")
    return Operation::McpServerLaunch;
  if (value == "mcp.server.connect")
    return Operation::McpServerConnect;
  if (value == "mcp.tool.call")
    return Operation::McpToolCall;
  if (value == "mcp.resource.read")
    return Operation::McpResourceRead;
  return std::nullopt;
}

namespace {

struct ParsedCommand
{
  bool ok = false;
  std::string reason;
  std::vector<std::string> argv;
};

std::string lowercase(std::string_view value)
{
  std::string result(value);
  std::ranges::transform(result, result.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return result;
}

bool contains_any(std::string_view value, std::vector<std::string_view> const& needles)
{
  return std::ranges::any_of(needles, [value](std::string_view needle) { return value.find(needle) != std::string_view::npos; });
}

bool equals_any(std::string_view value, std::vector<std::string_view> const& candidates)
{
  return std::ranges::any_of(candidates, [value](std::string_view candidate) { return value == candidate; });
}

bool is_shell_metacharacter(char ch)
{
  switch (ch)
  {
    case ';':
    case '&':
    case '|':
    case '<':
    case '>':
    case '`':
    case '$':
    case '(':
    case ')':
      return true;
    default:
      return false;
  }
}

ParsedCommand parse_command_argv(std::string_view command)
{
  ParsedCommand parsed;
  std::string current;
  char quote = '\0';
  bool escaping = false;

  for (char const ch : command)
  {
    auto const byte = static_cast<unsigned char>(ch);
    if (byte < 0x20 || byte == 0x7F)
    {
      parsed.reason = "command contains a forbidden control byte";
      return parsed;
    }
    if (escaping)
    {
      current.push_back(ch);
      escaping = false;
      continue;
    }
    if (ch == '\\')
    {
      escaping = true;
      continue;
    }
    if (quote != '\0')
    {
      if (ch == quote)
      {
        quote = '\0';
      }
      else
      {
        current.push_back(ch);
      }
      continue;
    }
    if (ch == '\'' || ch == '"')
    {
      quote = ch;
      continue;
    }
    if (is_shell_metacharacter(ch))
    {
      parsed.reason = "shell metacharacters are not supported by the command tool";
      return parsed;
    }
    if (std::isspace(byte) != 0)
    {
      if (!current.empty())
      {
        parsed.argv.push_back(current);
        current.clear();
      }
      continue;
    }
    current.push_back(ch);
  }

  if (escaping || quote != '\0')
  {
    parsed.reason = "unterminated command escape or quote";
    return parsed;
  }
  if (!current.empty())
    parsed.argv.push_back(current);
  if (parsed.argv.empty())
  {
    parsed.reason = "empty command";
    return parsed;
  }
  parsed.ok = true;
  return parsed;
}

std::vector<std::string> lowercase_argv(std::vector<std::string> const& argv)
{
  std::vector<std::string> result;
  result.reserve(argv.size());
  for (auto const& arg : argv) result.push_back(lowercase(arg));
  return result;
}

bool is_within_workspace(std::filesystem::path const& workspace_dir, std::filesystem::path const& target_path)
{
  if (target_path.empty())
  {
    return true;
  }

  std::error_code workspace_error;
  std::error_code target_error;
  auto const workspace_path = std::filesystem::weakly_canonical(workspace_dir, workspace_error);
  auto const target_path_normalized = std::filesystem::weakly_canonical(target_path, target_error);
  auto const workspace = workspace_error ? std::filesystem::absolute(workspace_dir).lexically_normal() : workspace_path;
  auto const target = target_error ? std::filesystem::absolute(target_path).lexically_normal() : target_path_normalized;
  if (target == workspace)
  {
    return true;
  }

  std::error_code relative_error;
  auto const relative = std::filesystem::relative(target, workspace, relative_error);
  if (relative_error || relative.empty())
  {
    return false;
  }
  auto const first = *relative.begin();
  return first != "..";
}

std::filesystem::path policy_path(std::filesystem::path const& path)
{
  std::error_code error;
  auto const normalized = std::filesystem::weakly_canonical(path, error);
  if (!error)
  {
    return normalized;
  }
  return std::filesystem::absolute(path).lexically_normal();
}

bool is_secret_path(std::filesystem::path const& path)
{
  for (auto const& part : path.lexically_normal())
  {
    auto const component = lowercase(part.string());
    if (equals_any(component, {".ssh", ".aws", ".gnupg", ".config/gcloud"}))
    {
      return true;
    }
  }

  auto const filename = lowercase(path.filename().string());
  if (filename == ".env" || filename.starts_with(".env."))
  {
    return filename != ".env.example";
  }
  if (equals_any(filename, {".npmrc", ".netrc", ".pypirc", ".gem/credentials", ".dockerconfigjson", "credentials.json", "auth.json", "config.json"}))
  {
    auto const full = lowercase(path.lexically_normal().string());
    return filename != "config.json" || contains_any(full, {"/.docker/", "\\.docker\\"});
  }

  auto const full = lowercase(path.string());
  return contains_any(full, {"id_rsa", "id_ed25519", "id_ecdsa", "id_dsa", "/.ssh/", "\\.ssh\\", "/.aws/", "\\.aws\\", "/.gnupg/", "\\.gnupg\\", "credentials",
                             "secret", "token"});
}

bool is_markdown_path(std::filesystem::path const& path)
{
  return lowercase(path.extension().string()) == ".md";
}

bool is_planning_markdown(std::filesystem::path const& path)
{
  auto const normalized = lowercase(path.lexically_normal().string());
  return is_markdown_path(path) &&
         (normalized.find("docs/") != std::string::npos || normalized.find("plan") != std::string::npos || normalized.find("version") != std::string::npos);
}

bool is_safe_relative_path_arg(std::string_view value)
{
  if (value.empty() || value.starts_with("-"))
    return true;
  std::filesystem::path const path(value);
  if (path.is_absolute())
    return false;
  for (auto const& part : path.lexically_normal())
  {
    if (part == "..")
      return false;
  }
  return !is_secret_path(path);
}

bool is_path_carrying_option(std::string_view value)
{
  auto const lower = lowercase(value);
  if (equals_any(lower, {"-o", "--output", "--git-dir", "--work-tree", "--exec-path", "--config-env"}))
  {
    return true;
  }
  return lower.starts_with("--output=") || lower.starts_with("--git-dir=") || lower.starts_with("--work-tree=") || lower.starts_with("--exec-path=") ||
         lower.starts_with("--config-env=");
}

bool has_unsafe_path_arg(std::vector<std::string> const& argv, std::size_t start)
{
  for (std::size_t index = start; index < argv.size(); ++index)
  {
    if (is_path_carrying_option(argv[index]))
      return true;
    if (!is_safe_relative_path_arg(argv[index]))
      return true;
  }
  return false;
}

bool is_dangerous_cmake_arg(std::string_view value)
{
  return value == "-e" || value == "-p" || value == "--install" || value == "--open";
}

bool is_simple_number(std::string_view value)
{
  if (value.empty())
    return false;
  bool saw_digit = false;
  bool saw_dot = false;
  for (char const ch : value)
  {
    if (std::isdigit(static_cast<unsigned char>(ch)) != 0)
    {
      saw_digit = true;
    }
    else if (ch == '.' && !saw_dot)
    {
      saw_dot = true;
    }
    else
    {
      return false;
    }
  }
  return saw_digit;
}

PermissionDecision decision(PermissionAction action, std::string reason, PermissionRisk risk)
{
  return PermissionDecision{.action = action, .reason = std::move(reason), .risk = risk};
}

PermissionRisk default_allow_risk(Operation operation)
{
  switch (operation)
  {
    case Operation::EditFile:
    case Operation::RunCommand:
    case Operation::NetworkFetch:
    case Operation::NetworkSearch:
    case Operation::LspServerLaunch:
    case Operation::SkillLoad:
    case Operation::TaskRun:
    case Operation::PluginExecute:
    case Operation::PluginToolCall:
    case Operation::PluginCommandRun:
    case Operation::PluginEventObserve:
    case Operation::McpServerLaunch:
    case Operation::McpServerConnect:
    case Operation::McpToolCall:
    case Operation::McpResourceRead:
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

  if ((request.operation == Operation::ReadFile || request.operation == Operation::EditFile || request.operation == Operation::LspQuery) &&
      is_secret_path(checked_path))
  {
    return decision(PermissionAction::Deny, "target looks like a secret file", PermissionRisk::Critical);
  }

  if (!is_within_workspace(request.workspace_dir, request.target_path))
  {
    return decision(PermissionAction::Ask, "target is outside the workspace", PermissionRisk::High);
  }

  if (request.operation == Operation::EditFile && request.mode == ava::agent::Mode::Plan && !is_planning_markdown(checked_path))
  {
    return decision(PermissionAction::Deny, "plan mode can only edit planning markdown", PermissionRisk::High);
  }

  if (request.operation == Operation::RunCommand)
  {
    if (request.command_metadata)
      return decide(*request.command_metadata);
    return classify_command(request.command);
  }

  if (request.operation == Operation::NetworkFetch)
  {
    return decision(PermissionAction::Ask, "network fetch requires explicit approval", PermissionRisk::Medium);
  }

  if (request.operation == Operation::NetworkSearch)
  {
    return decision(PermissionAction::Ask, "network search requires explicit approval", PermissionRisk::Medium);
  }

  if (request.operation == Operation::LspServerLaunch)
  {
    return decision(PermissionAction::Ask, "LSP server launch requires explicit approval", PermissionRisk::High);
  }

  if (request.operation == Operation::SkillLoad)
  {
    return decision(PermissionAction::Ask, "skill loading requires explicit approval", PermissionRisk::Medium);
  }

  if (request.operation == Operation::TaskRun)
  {
    return decision(PermissionAction::Ask, "subagent task execution requires explicit approval", PermissionRisk::Medium);
  }

  if (request.operation == Operation::PluginExecute)
  {
    return decision(PermissionAction::Ask, "plugin subprocess execution requires explicit approval", PermissionRisk::High);
  }

  if (request.operation == Operation::PluginToolCall)
  {
    return decision(PermissionAction::Ask, "plugin tool calls require explicit approval", PermissionRisk::High);
  }

  if (request.operation == Operation::PluginCommandRun)
  {
    return decision(PermissionAction::Ask, "plugin commands require explicit approval", PermissionRisk::High);
  }

  if (request.operation == Operation::PluginEventObserve)
  {
    return decision(PermissionAction::Ask, "plugin event observation requires explicit approval", PermissionRisk::Medium);
  }

  if (request.operation == Operation::McpServerLaunch)
  {
    return decision(PermissionAction::Ask, "MCP server launch requires explicit approval", PermissionRisk::High);
  }

  if (request.operation == Operation::McpServerConnect)
  {
    return decision(PermissionAction::Ask, "MCP server connection requires explicit approval", PermissionRisk::High);
  }

  if (request.operation == Operation::McpToolCall)
  {
    return decision(PermissionAction::Ask, "MCP tool calls require explicit approval", PermissionRisk::High);
  }

  if (request.operation == Operation::McpResourceRead)
  {
    return decision(PermissionAction::Ask, "MCP resource reads require explicit approval", PermissionRisk::Medium);
  }

  return decision(PermissionAction::Allow, "allowed by default workspace policy", default_allow_risk(request.operation));
}

CommandPermissionMetadata command_permission_metadata(ava::command::CommandPlan const& plan, bool unverified_delegated_executor)
{
  return command_permission_metadata(plan, CommandContainmentInfo{}, unverified_delegated_executor);
}

CommandPermissionMetadata command_permission_metadata(ava::command::CommandPlan const& plan, CommandContainmentInfo const& containment,
                                                      bool unverified_delegated_executor)
{
  auto const& classification = plan.classification();
  CommandContainmentStatus containment_status = CommandContainmentStatus::NotRequired;
  if (classification.capabilities.requires_containment || classification.capabilities.executes_mutable_project_code ||
      classification.level == ava::command::CommandLevel::Sensitive)
  {
    containment_status = containment.available ? CommandContainmentStatus::Available : CommandContainmentStatus::Unavailable;
  }
  CommandPermissionMetadata metadata{
      .level = classification.level,
      .family = classification.family,
      .fingerprint = plan.fingerprint(),
      .execution_domain = plan.execution_domain(),
      .resolved_executable = plan.resolved_executable() ? plan.resolved_executable()->executable.canonical_path : std::filesystem::path{},
      .executable_origin = plan.resolved_executable() ? plan.resolved_executable()->origin : ava::command::ExecutableOrigin::System,
      .cwd = plan.cwd(),
      .executes_mutable_project_code = classification.capabilities.executes_mutable_project_code,
      .containment_available = containment.available,
      .containment_status = containment_status,
      .containment_profile_id = containment.profile_id,
      .containment_network_allowed = containment.network_allowed,
      .backend_maximum_scope = classification.max_interactive_scope,
      .environment_profile_id = plan.environment_profile_id(),
      .environment_digest = plan.environment_digest(),
      .executor_identity_verified = !unverified_delegated_executor};
  // Until the separate stable recipe identity/store exists, every planned
  // command is one-shot only regardless of classification, containment, or
  // executor verification. No command may receive a reusable session or
  // workspace grant during this milestone.
  metadata.backend_maximum_scope = ava::command::InteractiveScope::Once;
  if (unverified_delegated_executor)
  {
    metadata.level = ava::command::CommandLevel::Critical;
    metadata.family = ava::command::CommandFamily::UnverifiedDelegatedExecutor;
    metadata.containment_available = false;
    metadata.containment_status = CommandContainmentStatus::UnverifiedDelegatedExecutor;
    metadata.backend_maximum_scope = ava::command::InteractiveScope::Once;
  }
  return metadata;
}

PermissionDecision decide(CommandPermissionMetadata const& metadata)
{
  if (!metadata.executor_identity_verified || metadata.containment_status == CommandContainmentStatus::UnverifiedDelegatedExecutor)
  {
    return decision(PermissionAction::Ask, "delegated command executor identity and environment are unverified", PermissionRisk::Critical);
  }
  if (metadata.level == ava::command::CommandLevel::Standard && !metadata.executes_mutable_project_code)
  {
    return decision(PermissionAction::Allow, "sealed command is a standard inspection recipe", PermissionRisk::Low);
  }
  if (metadata.level == ava::command::CommandLevel::Standard)
  {
    if (metadata.containment_available)
      return decision(PermissionAction::Allow, "sealed command executes mutable project code under verified development containment", PermissionRisk::Medium);
    return decision(PermissionAction::Ask, "sealed command executes mutable project code; containment is unavailable", PermissionRisk::High);
  }
  if (metadata.level == ava::command::CommandLevel::Sensitive)
  {
    return decision(PermissionAction::Ask, "sealed command has sensitive capabilities", PermissionRisk::High);
  }
  return decision(PermissionAction::Ask, "sealed command is critical", PermissionRisk::Critical);
}

bool command_permission_allows_reusable_grant(CommandPermissionMetadata const& metadata) noexcept
{
  // Every planned command is one-shot only during this milestone, so no
  // command metadata may receive a reusable session or workspace grant.
  static_cast<void>(metadata);
  return false;
}

bool command_prompt_allows_persistent_allow(PermissionPrompt const& prompt) noexcept
{
  return !prompt.command_metadata || command_permission_allows_reusable_grant(*prompt.command_metadata);
}

bool is_repository_controlled_build_or_test_command(std::string_view command)
{
  auto const parsed = parse_command_argv(command);
  if (!parsed.ok)
    return false;

  auto const argv = lowercase_argv(parsed.argv);
  auto const executable = lowercase(std::filesystem::path(argv.front()).filename().string());
  if (executable == "ctest")
    return true;
  return executable == "cmake" && std::ranges::any_of(argv, [](std::string const& arg) {
           return arg == "--build" || arg.starts_with("--build=") || arg == "--build-and-test" || arg == "--workflow";
         });
}

PermissionDecision classify_command(std::string_view command)
{
  auto const parsed = parse_command_argv(command);
  if (!parsed.ok)
  {
    return decision(PermissionAction::Deny, parsed.reason, PermissionRisk::High);
  }
  auto const argv = lowercase_argv(parsed.argv);
  auto const& executable = argv[0];
  auto const value = lowercase(command);

  if (contains_any(value, {"rm -rf", "rm -fr", "mkfs", ":(){", "chmod -r 777 /", "chown -r ", "> /dev/"}))
  {
    return decision(PermissionAction::Deny, "command matches a destructive pattern", PermissionRisk::Critical);
  }

  if (equals_any(executable, {"bash", "sh", "zsh", "fish", "python", "python3", "node", "perl", "ruby", "php", "lua", "make", "ninja", "npm", "pnpm", "yarn",
                              "bun", "cargo"}))
  {
    return decision(PermissionAction::Deny, "command can execute arbitrary scripts", PermissionRisk::High);
  }

  if (contains_any(value, {"git push", "git reset --hard", "git clean", "npm publish", "pnpm publish", "yarn publish", "deploy", "terraform apply",
                           "kubectl delete", "sudo "}))
  {
    return decision(PermissionAction::Ask, "command can change external or destructive state", PermissionRisk::High);
  }

  if (executable == "git" && argv.size() >= 2 && (argv[1] == "status" || argv[1] == "diff" || argv[1] == "log"))
  {
    if (!has_unsafe_path_arg(parsed.argv, 2))
    {
      return decision(PermissionAction::Allow, "command is read-only or local verification", PermissionRisk::Low);
    }
    return decision(PermissionAction::Ask, "git command includes unsafe path or output options", PermissionRisk::Medium);
  }
  if (executable == "cmake")
  {
    for (auto const& arg : argv)
    {
      if (is_dangerous_cmake_arg(arg))
      {
        return decision(PermissionAction::Deny, "cmake script/file helpers are not allowed", PermissionRisk::High);
      }
    }
  }
  if (is_repository_controlled_build_or_test_command(command))
  {
    return decision(PermissionAction::Ask, "repository build or test execution requires explicit approval", PermissionRisk::High);
  }
  if (executable == "rg")
  {
    for (auto const& arg : argv)
    {
      if (arg == "--pre" || arg.starts_with("--pre="))
      {
        return decision(PermissionAction::Deny, "rg preprocessors can execute arbitrary commands", PermissionRisk::High);
      }
    }
    if (!has_unsafe_path_arg(parsed.argv, 1))
    {
      return decision(PermissionAction::Allow, "command is read-only or local verification", PermissionRisk::Low);
    }
  }
  if (executable == "ls" && !has_unsafe_path_arg(parsed.argv, 1))
  {
    return decision(PermissionAction::Allow, "command is read-only or local verification", PermissionRisk::Low);
  }
  if (executable == "pwd" && (argv.size() == 1 || (argv.size() == 2 && (argv[1] == "-p" || argv[1] == "-l"))))
  {
    return decision(PermissionAction::Allow, "command is read-only or local verification", PermissionRisk::Low);
  }
  if (executable == "sleep" && argv.size() == 2 && is_simple_number(argv[1]))
  {
    return decision(PermissionAction::Allow, "command is read-only or local verification", PermissionRisk::Low);
  }

  return decision(PermissionAction::Ask, "command risk is unknown", PermissionRisk::Medium);
}

std::string to_string(PermissionAction action)
{
  switch (action)
  {
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
  switch (resolution)
  {
    case PermissionResolution::Allow:
    case PermissionResolution::AllowSessionGrant:
      return "allow";
    case PermissionResolution::Deny:
      return "deny";
    case PermissionResolution::Cancel:
      return "cancel";
  }
  return "deny";
}

std::string to_string(PermissionResolutionDecision const& decision)
{
  return to_string(decision.resolution);
}

std::string to_string(CommandContainmentStatus status)
{
  switch (status)
  {
    case CommandContainmentStatus::NotRequired:
      return "not_required";
    case CommandContainmentStatus::Unavailable:
      return "unavailable";
    case CommandContainmentStatus::Available:
      return "available";
    case CommandContainmentStatus::Active:
      return "active";
    case CommandContainmentStatus::UnverifiedDelegatedExecutor:
      return "unverified_delegated_executor";
  }
  return "unavailable";
}

std::string to_string(PermissionRisk risk)
{
  switch (risk)
  {
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
  switch (operation)
  {
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
    case Operation::NetworkSearch:
      return "network.search";
    case Operation::LspServerLaunch:
      return "lsp.server.launch";
    case Operation::LspQuery:
      return "lsp.query";
    case Operation::SkillLoad:
      return "skill";
    case Operation::TaskRun:
      return "task";
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
    case Operation::McpResourceRead:
      return "mcp.resource.read";
  }
  return "unknown";
}

}  // namespace ava::permissions
