#include "ava/tools/core_tools.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ava/tools/path_guard.hpp"
#include "shell_runner.hpp"

namespace ava::tools {
namespace {

const std::string kFileHistoryPathFragment = std::string{kAvaDirectoryName} + "/" + kFileHistoryDirectoryName;

[[nodiscard]] std::string lowercase(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value;
}

[[nodiscard]] std::string remove_backup_history_lines(const std::string& output) {
  std::istringstream stream(output);
  std::ostringstream filtered;
  std::string line;
  bool first = true;
  while(std::getline(stream, line)) {
    if(line.find(kFileHistoryPathFragment) != std::string::npos) {
      continue;
    }
    if(!first) {
      filtered << '\n';
    }
    filtered << line;
    first = false;
  }
  if(!output.empty() && output.ends_with('\n') && !first) {
    filtered << '\n';
  }
  return filtered.str();
}

[[nodiscard]] bool contains_shell_metacharacters(std::string_view command) {
  return command.find_first_of(";|&$<>`()[]{}\n") != std::string_view::npos;
}

[[nodiscard]] bool contains_git_quote_or_escape(std::string_view command) {
  return command.find_first_of("'\"\\") != std::string_view::npos;
}

[[nodiscard]] bool contains_mutating_git_patterns(const std::string& lower) {
  static const std::vector<std::string> kMutatingPatterns = {
      " push",       " commit",      " reset",       " checkout",   " merge",   " rebase",
      " cherry-pick", " branch -d",   " branch --delete", " tag -d",     " tag --delete", " remote add",
      " remote remove", " stash push", " stash pop",       " apply",      " am ",          " revert",};

  return std::any_of(kMutatingPatterns.begin(), kMutatingPatterns.end(), [&](const auto& pattern) {
    return lower.find(pattern) != std::string::npos;
  });
}

[[nodiscard]] bool has_forbidden_git_path_or_option(const std::string& subcommand) {
  std::istringstream iss(subcommand);
  std::string token;
  while(iss >> token) {
    const auto lower = lowercase(token);
    if(lower == "--no-index" || lower == "--output" || lower.rfind("--output=", 0) == 0 || lower == "-o" ||
       lower == "--ext-diff" || lower == "--textconv" || lower.rfind("--ext-diff=", 0) == 0 ||
       lower.rfind("--textconv=", 0) == 0 || lower == "--git-dir" || lower.rfind("--git-dir=", 0) == 0 ||
       lower == "--work-tree" || lower.rfind("--work-tree=", 0) == 0 || lower == "-c" || lower == "--ignored" ||
       lower == "--contents" || lower.rfind("--contents=", 0) == 0 || lower == "-s" ||
       lower == "--ignore-revs-file" || lower.rfind("--ignore-revs-file=", 0) == 0 || lower == "-d" ||
       lower == "-D" || lower == "-m" || lower == "-M" || lower == "-f" || lower == "--force" ||
       lower == "--delete" || lower == "--move" || lower == "--set-upstream-to" || lower == "--unset-upstream" ||
       lower == "add" || lower == "remove" || lower == "rename" || lower == "set-url" || lower == "set-head" ||
       lower == "prune" || lower == "update") {
      return true;
    }
    if(token.starts_with('/') || token == ".." || token.starts_with("../") || token.find("/../") != std::string::npos ||
       lower.find(kFileHistoryPathFragment) != std::string::npos ||
       lower.find(kFileHistoryDirectoryName) != std::string::npos ||
       token.find('*') != std::string::npos || token.find('?') != std::string::npos || token.starts_with('~')) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] bool is_safe_git_subcommand(const std::string& lower_subcommand) {
  std::istringstream iss(lower_subcommand);
  std::string first;
  iss >> first;
  if(first.empty()) {
    return false;
  }

  if(first == "status" || first == "log" || first == "diff" || first == "show" || first == "blame" ||
     first == "rev-parse" || first == "describe" || first == "ls-files" || first == "shortlog" ||
     first == "rev-list" || first == "cat-file" || first == "for-each-ref") {
    return true;
  }

  if(first == "branch") {
    std::string second;
    iss >> second;
    return second.empty() || second == "--list" || second == "-l" || second == "-v" || second == "-vv" ||
           second == "-a" || second == "-r" || second == "--show-current" || second == "--contains" ||
           second == "--merged" || second == "--no-merged";
  }

  if(first == "tag") {
    std::string second;
    iss >> second;
    return second.empty() || second == "-l" || second == "--list" || second == "-n";
  }

  if(first == "remote") {
    std::string second;
    iss >> second;
    return second == "-v" || second == "show";
  }

  if(first == "stash") {
    std::string second;
    iss >> second;
    return second == "list" || second == "show";
  }

  return false;
}

}  // namespace

GitReadTool::GitReadTool(std::filesystem::path workspace_root)
    : workspace_root_(normalize_workspace_root(workspace_root)),
      tool_name_("git") {}

std::string GitReadTool::name() const {
  return tool_name_;
}

std::string GitReadTool::description() const {
  return "Run read-only git commands (status, log, diff, show, blame, etc.)";
}

std::string GitReadTool::search_hint() const {
  return "git status log diff show blame branch tag remote rev-parse";
}

nlohmann::json GitReadTool::parameters() const {
  return nlohmann::json{
      {"type", "object"},
      {"required", nlohmann::json::array({"command"})},
      {"properties",
       {{"command",
         {{"type", "string"},
          {"description", "Read-only git subcommand and arguments; the git prefix is added automatically"}}}}},
  };
}

ava::types::ToolResult GitReadTool::execute(const nlohmann::json& args) const {
  if(!args.contains("command")) {
    throw std::runtime_error("missing required field: command");
  }

  const auto subcommand = args.at("command").get<std::string>();
  if(contains_shell_metacharacters(subcommand) || contains_git_quote_or_escape(subcommand)) {
    throw std::runtime_error("git command contains disallowed shell metacharacters");
  }

  const auto lower_subcommand = lowercase(subcommand);
  if(!is_safe_git_subcommand(lower_subcommand) || contains_mutating_git_patterns(" " + lower_subcommand) ||
     has_forbidden_git_path_or_option(subcommand)) {
    throw std::runtime_error(
        "git command not allowed in read-only mode: " + subcommand +
        ". Only read-only git commands are permitted: status, log, diff, show, blame, branch, tag, remote, rev-parse, ls-files, describe, shortlog, stash list."
    );
  }

  const auto outcome = run_shell_command(
      "env GIT_OPTIONAL_LOCKS=0 GIT_EXTERNAL_DIFF= GIT_PAGER=cat git " + subcommand,
      workspace_root_,
      120000
  );
  const auto content = render_shell_result(CommandOutcome{
      .output = remove_backup_history_lines(outcome.output),
      .exit_code = outcome.exit_code,
  });

  return ava::types::ToolResult{.call_id = "", .content = content, .is_error = outcome.exit_code != 0};
}

GitReadAliasTool::GitReadAliasTool(std::filesystem::path workspace_root)
    : GitReadTool(std::move(workspace_root)) {}

std::string GitReadAliasTool::name() const {
  return "git_read";
}

}  // namespace ava::tools
