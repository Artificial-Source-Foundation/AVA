#include "ava/permissions/permission.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

namespace ava::permissions {

namespace {

struct ParsedCommand {
  bool ok = false;
  std::string reason;
  std::vector<std::string> argv;
};

std::string lowercase(std::string_view value) {
  std::string result(value);
  std::ranges::transform(result, result.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return result;
}

bool contains_any(std::string_view value, const std::vector<std::string_view>& needles) {
  return std::ranges::any_of(needles, [value](std::string_view needle) { return value.find(needle) != std::string_view::npos; });
}

bool equals_any(std::string_view value, const std::vector<std::string_view>& candidates) {
  return std::ranges::any_of(candidates, [value](std::string_view candidate) { return value == candidate; });
}

bool is_shell_metacharacter(char ch) {
  switch (ch) {
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

ParsedCommand parse_command_argv(std::string_view command) {
  ParsedCommand parsed;
  std::string current;
  char quote = '\0';
  bool escaping = false;

  for (const char ch : command) {
    const auto byte = static_cast<unsigned char>(ch);
    if (byte < 0x20 || byte == 0x7F) {
      parsed.reason = "command contains a forbidden control byte";
      return parsed;
    }
    if (escaping) {
      current.push_back(ch);
      escaping = false;
      continue;
    }
    if (ch == '\\') {
      escaping = true;
      continue;
    }
    if (quote != '\0') {
      if (ch == quote) {
        quote = '\0';
      } else {
        current.push_back(ch);
      }
      continue;
    }
    if (ch == '\'' || ch == '"') {
      quote = ch;
      continue;
    }
    if (is_shell_metacharacter(ch)) {
      parsed.reason = "shell metacharacters are not supported by the command tool";
      return parsed;
    }
    if (std::isspace(byte) != 0) {
      if (!current.empty()) {
        parsed.argv.push_back(current);
        current.clear();
      }
      continue;
    }
    current.push_back(ch);
  }

  if (escaping || quote != '\0') {
    parsed.reason = "unterminated command escape or quote";
    return parsed;
  }
  if (!current.empty()) parsed.argv.push_back(current);
  if (parsed.argv.empty()) {
    parsed.reason = "empty command";
    return parsed;
  }
  parsed.ok = true;
  return parsed;
}

std::vector<std::string> lowercase_argv(const std::vector<std::string>& argv) {
  std::vector<std::string> result;
  result.reserve(argv.size());
  for (const auto& arg : argv) result.push_back(lowercase(arg));
  return result;
}

bool is_within_workspace(const std::filesystem::path& workspace_dir, const std::filesystem::path& target_path) {
  if (target_path.empty()) {
    return true;
  }

  std::error_code workspace_error;
  std::error_code target_error;
  const auto workspace_path = std::filesystem::weakly_canonical(workspace_dir, workspace_error);
  const auto target_path_normalized = std::filesystem::weakly_canonical(target_path, target_error);
  const auto workspace = workspace_error ? std::filesystem::absolute(workspace_dir).lexically_normal() : workspace_path;
  const auto target = target_error ? std::filesystem::absolute(target_path).lexically_normal() : target_path_normalized;
  if (target == workspace) {
    return true;
  }

  std::error_code relative_error;
  const auto relative = std::filesystem::relative(target, workspace, relative_error);
  if (relative_error || relative.empty()) {
    return false;
  }
  const auto first = *relative.begin();
  return first != "..";
}

std::filesystem::path policy_path(const std::filesystem::path& path) {
  std::error_code error;
  const auto normalized = std::filesystem::weakly_canonical(path, error);
  if (!error) {
    return normalized;
  }
  return std::filesystem::absolute(path).lexically_normal();
}

bool is_secret_path(const std::filesystem::path& path) {
  for (const auto& part : path.lexically_normal()) {
    const auto component = lowercase(part.string());
    if (equals_any(component, {".ssh", ".aws", ".gnupg", ".config/gcloud"})) {
      return true;
    }
  }

  const auto filename = lowercase(path.filename().string());
  if (filename == ".env" || filename.starts_with(".env.")) {
    return filename != ".env.example";
  }
  if (equals_any(filename, {".npmrc", ".netrc", ".pypirc", ".gem/credentials", ".dockerconfigjson",
                            "credentials.json", "auth.json", "config.json"})) {
    const auto full = lowercase(path.lexically_normal().string());
    return filename != "config.json" || contains_any(full, {"/.docker/", "\\.docker\\"});
  }

  const auto full = lowercase(path.string());
  return contains_any(full, {"id_rsa",     "id_ed25519", "id_ecdsa", "id_dsa", "/.ssh/", "\\.ssh\\",
                             "/.aws/",    "\\.aws\\",  "/.gnupg/", "\\.gnupg\\", "credentials", "secret",
                             "token"});
}

bool is_markdown_path(const std::filesystem::path& path) {
  return lowercase(path.extension().string()) == ".md";
}

bool is_planning_markdown(const std::filesystem::path& path) {
  const auto normalized = lowercase(path.lexically_normal().string());
  return is_markdown_path(path) &&
         (normalized.find("docs/") != std::string::npos || normalized.find("plan") != std::string::npos ||
          normalized.find("version") != std::string::npos);
}

bool is_safe_relative_path_arg(std::string_view value) {
  if (value.empty() || value.starts_with("-")) return true;
  const std::filesystem::path path(value);
  if (path.is_absolute()) return false;
  for (const auto& part : path.lexically_normal()) {
    if (part == "..") return false;
  }
  return !is_secret_path(path);
}

bool is_path_carrying_option(std::string_view value) {
  const auto lower = lowercase(value);
  if (equals_any(lower, {"-o", "--output", "--git-dir", "--work-tree", "--exec-path", "--config-env"})) {
    return true;
  }
  return lower.starts_with("--output=") || lower.starts_with("--git-dir=") || lower.starts_with("--work-tree=") ||
         lower.starts_with("--exec-path=") || lower.starts_with("--config-env=");
}

bool has_unsafe_path_arg(const std::vector<std::string>& argv, std::size_t start) {
  for (std::size_t index = start; index < argv.size(); ++index) {
    if (is_path_carrying_option(argv[index])) return true;
    if (!is_safe_relative_path_arg(argv[index])) return true;
  }
  return false;
}

bool is_dangerous_cmake_arg(std::string_view value) {
  return value == "-e" || value == "-p" || value == "--install" || value == "--open";
}

bool is_safe_cmake_build(const std::vector<std::string>& argv, const std::vector<std::string>& lower) {
  if (argv.size() < 3 || lower[1] != "--build") return false;
  if (!is_safe_relative_path_arg(argv[2])) return false;
  for (std::size_t index = 3; index < argv.size(); ++index) {
    const auto& arg = lower[index];
    if (is_dangerous_cmake_arg(arg) || arg == "--install" || arg == "--build-and-test") return false;
    if (arg.starts_with("--") || arg.starts_with("-j") || arg == "-v") continue;
    if (!is_safe_relative_path_arg(argv[index])) return false;
  }
  return true;
}

bool is_safe_ctest(const std::vector<std::string>& argv, const std::vector<std::string>& lower) {
  for (std::size_t index = 1; index < argv.size(); ++index) {
    const auto& arg = lower[index];
    if (arg == "--build-and-test" || arg == "--test-command" || arg == "--build-generator" ||
        arg == "--build-makeprogram") {
      return false;
    }
    if (!is_safe_relative_path_arg(argv[index])) return false;
  }
  return true;
}

bool is_simple_number(std::string_view value) {
  if (value.empty()) return false;
  bool saw_digit = false;
  bool saw_dot = false;
  for (const char ch : value) {
    if (std::isdigit(static_cast<unsigned char>(ch)) != 0) {
      saw_digit = true;
    } else if (ch == '.' && !saw_dot) {
      saw_dot = true;
    } else {
      return false;
    }
  }
  return saw_digit;
}

}  // namespace

PermissionDecision decide(const PermissionRequest& request) {
  const auto checked_path = policy_path(request.target_path);

  if ((request.operation == Operation::ReadFile || request.operation == Operation::EditFile) &&
      is_secret_path(checked_path)) {
    return {.action = PermissionAction::Deny, .reason = "target looks like a secret file"};
  }

  if (!is_within_workspace(request.workspace_dir, request.target_path)) {
    return {.action = PermissionAction::Ask, .reason = "target is outside the workspace"};
  }

  if (request.operation == Operation::EditFile && request.mode == ava::agent::Mode::Plan &&
      !is_planning_markdown(checked_path)) {
    return {.action = PermissionAction::Deny, .reason = "plan mode can only edit planning markdown"};
  }

  if (request.operation == Operation::RunCommand) {
    return classify_command(request.command);
  }

  return {.action = PermissionAction::Allow, .reason = "allowed by default workspace policy"};
}

PermissionDecision classify_command(std::string_view command) {
  const auto parsed = parse_command_argv(command);
  if (!parsed.ok) {
    return {.action = PermissionAction::Deny, .reason = parsed.reason};
  }
  const auto argv = lowercase_argv(parsed.argv);
  const auto& executable = argv[0];
  const auto value = lowercase(command);

  if (contains_any(value, {"rm -rf", "rm -fr", "mkfs", ":(){", "chmod -r 777 /", "chown -r ", "> /dev/"})) {
    return {.action = PermissionAction::Deny, .reason = "command matches a destructive pattern"};
  }

  if (equals_any(executable, {"bash", "sh", "zsh", "fish", "python", "python3", "node", "perl", "ruby", "php",
                              "lua", "make", "ninja", "npm", "pnpm", "yarn", "bun", "cargo"})) {
    return {.action = PermissionAction::Deny, .reason = "command can execute arbitrary scripts"};
  }

  if (contains_any(value, {"git push", "git reset --hard", "git clean", "npm publish", "pnpm publish", "yarn publish",
                          "deploy", "terraform apply", "kubectl delete", "sudo "})) {
    return {.action = PermissionAction::Ask, .reason = "command can change external or destructive state"};
  }

  if (executable == "git" && argv.size() >= 2 && (argv[1] == "status" || argv[1] == "diff" || argv[1] == "log")) {
    if (!has_unsafe_path_arg(parsed.argv, 2)) {
      return {.action = PermissionAction::Allow, .reason = "command is read-only or local verification"};
    }
    return {.action = PermissionAction::Ask, .reason = "git command includes unsafe path or output options"};
  }
  if (executable == "cmake") {
    for (const auto& arg : argv) {
      if (is_dangerous_cmake_arg(arg)) {
        return {.action = PermissionAction::Deny, .reason = "cmake script/file helpers are not allowed"};
      }
    }
    if (is_safe_cmake_build(parsed.argv, argv)) {
      return {.action = PermissionAction::Allow, .reason = "command is read-only or local verification"};
    }
  }
  if (executable == "ctest" && is_safe_ctest(parsed.argv, argv)) {
    return {.action = PermissionAction::Allow, .reason = "command is read-only or local verification"};
  }
  if (executable == "rg") {
    for (const auto& arg : argv) {
      if (arg == "--pre" || arg.starts_with("--pre=")) {
        return {.action = PermissionAction::Deny, .reason = "rg preprocessors can execute arbitrary commands"};
      }
    }
    if (!has_unsafe_path_arg(parsed.argv, 1)) {
      return {.action = PermissionAction::Allow, .reason = "command is read-only or local verification"};
    }
  }
  if (executable == "ls" && !has_unsafe_path_arg(parsed.argv, 1)) {
    return {.action = PermissionAction::Allow, .reason = "command is read-only or local verification"};
  }
  if (executable == "pwd" && (argv.size() == 1 || (argv.size() == 2 && (argv[1] == "-p" || argv[1] == "-l")))) {
    return {.action = PermissionAction::Allow, .reason = "command is read-only or local verification"};
  }
  if (executable == "sleep" && argv.size() == 2 && is_simple_number(argv[1])) {
    return {.action = PermissionAction::Allow, .reason = "command is read-only or local verification"};
  }

  return {.action = PermissionAction::Ask, .reason = "command risk is unknown"};
}

std::string to_string(PermissionAction action) {
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

std::string to_string(PermissionResolution resolution) {
  switch (resolution) {
    case PermissionResolution::Allow:
      return "allow";
    case PermissionResolution::Deny:
      return "deny";
  }
  return "deny";
}

std::string to_string(Operation operation) {
  switch (operation) {
    case Operation::ReadFile:
      return "read";
    case Operation::SearchFiles:
      return "search";
    case Operation::EditFile:
      return "edit";
    case Operation::RunCommand:
      return "bash";
  }
  return "unknown";
}

}  // namespace ava::permissions
