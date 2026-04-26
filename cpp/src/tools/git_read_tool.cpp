#include "ava/tools/git_read_tool.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ava/config/paths.hpp"
#include "ava/core/string_utils.hpp"
#include "ava/tools/file_backup.hpp"
#include "ava/tools/path_guard.hpp"
#include "shell_runner.hpp"

namespace ava::tools {
namespace {

const std::string kLegacyFileHistoryPathFragment = std::string{kLegacyProjectAvaDirectoryName} + "/" + kLegacyProjectFileHistoryDirectoryName;

[[nodiscard]] std::string path_with_slashes(std::filesystem::path path) {
  return path.lexically_normal().generic_string();
}

[[nodiscard]] bool contains_path_fragment(std::string_view value, const std::string& fragment) {
  return !fragment.empty() && value.find(fragment) != std::string_view::npos;
}

[[nodiscard]] std::string current_file_history_fragment_for_workspace(const std::filesystem::path& workspace_root) {
  const auto current_history_dir = ava::config::file_history_dir().lexically_normal();
  std::error_code ec;
  auto relative = std::filesystem::relative(current_history_dir, workspace_root.lexically_normal(), ec);
  const auto first_component = relative.begin();
  if(ec || relative.empty() || (first_component != relative.end() && *first_component == "..")) {
    return {};
  }
  return path_with_slashes(std::move(relative));
}

[[nodiscard]] bool contains_backup_history_output_path(const std::string& line, const std::filesystem::path& workspace_root) {
  const auto lower = ava::core::lowercase_ascii(line);
  if(lower.find(kLegacyFileHistoryPathFragment) != std::string::npos) {
    return true;
  }

  const auto current_history_dir = path_with_slashes(ava::config::file_history_dir());
  const auto line_path = path_with_slashes(line);
  if(contains_path_fragment(line_path, current_history_dir)) {
    return true;
  }
  return contains_path_fragment(line_path, current_file_history_fragment_for_workspace(workspace_root));
}

[[nodiscard]] std::string remove_backup_history_lines(const std::string& output, const std::filesystem::path& workspace_root) {
  std::istringstream stream(output);
  std::ostringstream filtered;
  std::string line;
  bool first = true;
  while(std::getline(stream, line)) {
    if(contains_backup_history_output_path(line, workspace_root)) {
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

[[nodiscard]] std::string redact_git_remote_credentials(const std::string& output, bool include_scp_like_urls) {
  static const std::regex kUrlUserInfoPattern{R"(([A-Za-z][A-Za-z0-9+.-]*://)[^/\s]*@)"};
  static const std::regex kUrlQueryFragmentPattern{R"(([A-Za-z][A-Za-z0-9+.-]*://[^\s?#]+)([?#][^\s]+))"};
  static const std::regex kScpLikeUserInfoPattern{R"((^|\s)[^\s/]*@((?:\[[^\]\s]+\]|[^\s/:]+):[^\s]+))"};
  static const std::regex kScpLikeQueryFragmentPattern{R"((^|\s)((?:[^\s/@]+@)?(?:\[[^\]\s]+\]|[^\s/:]+):[^\s?#]+)([?#][^\s]+))"};
  auto redacted = std::regex_replace(output, kUrlUserInfoPattern, "$1***@");
  redacted = std::regex_replace(redacted, kUrlQueryFragmentPattern, "$1?***");
  if(include_scp_like_urls) {
    redacted = std::regex_replace(redacted, kScpLikeUserInfoPattern, "$1***@$2");
    redacted = std::regex_replace(redacted, kScpLikeQueryFragmentPattern, "$1$2?***");
  }
  return redacted;
}

[[nodiscard]] bool contains_shell_metacharacters(std::string_view command) {
  return command.find_first_of(";|&$<>`()[]\n") != std::string_view::npos;
}

[[nodiscard]] bool contains_git_quote_or_escape(std::string_view command) {
  return command.find_first_of("'\"\\") != std::string_view::npos;
}

[[nodiscard]] bool is_git_revision_pathspec(std::string_view token) {
  const auto colon = token.find(':');
  if(colon == std::string_view::npos || colon + 1 >= token.size()) {
    return false;
  }
  if(colon == 0) {
    return token.size() > 1;
  }
  if(token.starts_with("--")) {
    return false;
  }
  return true;
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

[[nodiscard]] bool has_forbidden_git_path_or_option(const std::string& subcommand, const std::filesystem::path& workspace_root) {
  const auto current_history_fragment = current_file_history_fragment_for_workspace(workspace_root);
  std::istringstream iss(subcommand);
  std::string token;
  bool is_first_token = true;
  bool is_ls_files = false;
  while(iss >> token) {
    const auto lower = ava::core::lowercase_ascii(token);
    if(is_first_token) {
      is_ls_files = lower == "ls-files";
      is_first_token = false;
    }
    const auto is_single_dash_ignored_cluster = lower.size() > 2 && lower.starts_with('-') && !lower.starts_with("--") &&
                                                lower.find('i') != std::string::npos;
    const auto is_ignored_long_abbreviation = lower.starts_with("--i") && std::string_view("--ignored").starts_with(lower);
    const auto is_ls_files_others_abbreviation = lower.starts_with("--o") && std::string_view("--others").starts_with(lower);
    const auto is_single_dash_others_cluster = lower.size() > 1 && lower.starts_with('-') && !lower.starts_with("--") &&
                                               lower.find('o') != std::string::npos;
    if(is_ls_files && (lower == "--others" || is_ls_files_others_abbreviation || lower == "-o" ||
                       is_single_dash_others_cluster)) {
      return true;
    }
    if(lower == "--no-index" || lower == "--output" || lower.rfind("--output=", 0) == 0 || lower == "-o" ||
       lower == "--ext-diff" || lower == "--textconv" || lower.rfind("--ext-diff=", 0) == 0 ||
       lower.rfind("--textconv=", 0) == 0 || lower == "--git-dir" || lower.rfind("--git-dir=", 0) == 0 ||
       lower == "--work-tree" || lower.rfind("--work-tree=", 0) == 0 || lower == "-c" || lower == "--ignored" ||
       lower.rfind("--ignored=", 0) == 0 || is_ignored_long_abbreviation || lower == "-i" || is_single_dash_ignored_cluster || lower == "--contents" || lower.rfind("--contents=", 0) == 0 ||
       lower == "--show-signature" || lower.rfind("--show-signature=", 0) == 0 || lower == "--format" ||
       lower.rfind("--format=", 0) == 0 || lower == "--pretty" || lower.rfind("--pretty=", 0) == 0 ||
       lower == "--ignore-revs-file" || lower.rfind("--ignore-revs-file=", 0) == 0 || lower == "-d" ||
       lower == "-D" || lower == "-m" || lower == "-M" || lower == "-f" || lower == "--force" ||
       lower == "--delete" || lower == "--move" || lower == "--set-upstream-to" || lower == "--unset-upstream" ||
       lower == "add" || lower == "remove" || lower == "rename" || lower == "set-url" || lower == "set-head" ||
       lower == "prune" || lower == "update") {
      return true;
    }
    if(token.starts_with('/') || token == ".." || token.starts_with("../") || token.find("/../") != std::string::npos ||
       lower.find(kLegacyFileHistoryPathFragment) != std::string::npos ||
       lower.find(kLegacyProjectFileHistoryDirectoryName) != std::string::npos ||
       contains_path_fragment(path_with_slashes(token), current_history_fragment) || is_git_revision_pathspec(token) ||
       token.find('*') != std::string::npos || token.find('?') != std::string::npos || token.starts_with('~')) {
      return true;
    }
  }
  return false;
}

[[nodiscard]] std::vector<std::string> split_tokens(const std::string& command) {
  std::istringstream iss(command);
  std::vector<std::string> tokens;
  std::string token;
  while(iss >> token) {
    tokens.push_back(ava::core::lowercase_ascii(token));
  }
  return tokens;
}

[[nodiscard]] bool contains_patch_or_object_output(const std::string& lower_subcommand) {
  const auto tokens = split_tokens(lower_subcommand);
  if(tokens.empty()) {
    return true;
  }

  const auto has_safe_diff_summary_flag = std::any_of(tokens.begin() + 1, tokens.end(), [](const auto& token) {
    return token == "--name-only" || token == "--name-status" || token == "--stat" || token.starts_with("--stat=");
  });
  const auto has_patch_output_flag = std::any_of(tokens.begin() + 1, tokens.end(), [](const auto& token) {
    const auto is_single_dash_patch_cluster = token.size() > 2 && token.starts_with('-') && !token.starts_with("--") &&
                                             token.find('p') != std::string::npos;
    return token.starts_with("-p") || token == "--patch" || token == "-u" || token == "--binary" ||
           is_single_dash_patch_cluster ||
           token == "--patch-with-stat" || token == "--patch-with-raw" || token.starts_with("--patch=") ||
           token.starts_with("--patch-with-") || token.starts_with("--word-diff") || token.starts_with("-u") ||
           token.starts_with("-U") || token.starts_with("--unified") || token.starts_with("--color-words") ||
           token == "--check" || token == "-c" || token == "--cc" || token == "--dd" ||
           token == "--remerge-diff" || token == "--diff-merges" || token.starts_with("--diff-merges=");
  });
  if(tokens.front() == "cat-file") {
    return true;
  }
  if(tokens.front() == "show") {
    return true;
  }
  if(tokens.front() == "diff") {
    if(has_patch_output_flag) {
      return true;
    }
    return !has_safe_diff_summary_flag;
  }
  if(tokens.front() == "status") {
    return std::any_of(tokens.begin() + 1, tokens.end(), [](const auto& token) {
      const auto is_single_dash_verbose_cluster = token.size() > 2 && token.starts_with('-') && !token.starts_with("--") &&
                                                  token.find('v') != std::string::npos;
      return token.starts_with("-v") || is_single_dash_verbose_cluster || token == "--verbose" ||
             token.starts_with("--verbose=");
    });
  }
  if(tokens.front() == "log" || tokens.front() == "stash") {
    return has_patch_output_flag;
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

  if(first == "status" || first == "log" || first == "diff" || first == "show" || first == "rev-parse" ||
     first == "describe" || first == "ls-files" || first == "shortlog" || first == "rev-list" ||
     first == "cat-file" || first == "for-each-ref") {
    return true;
  }

  if(first == "branch") {
    std::string second;
    iss >> second;
    if(second.empty()) {
      return true;
    }
    if(second == "--contains" || second == "--merged" || second == "--no-merged") {
      std::string first_arg;
      std::string extra;
      iss >> first_arg;
      return !(iss >> extra);
    }
    if(second == "--list" || second == "-l" || second == "-v" || second == "-vv" || second == "-a" ||
       second == "-r" || second == "--show-current") {
      std::string extra;
      return !(iss >> extra);
    }
    return false;
  }

  if(first == "tag") {
    std::string second;
    iss >> second;
    return second.empty() || second == "-l" || second == "--list" || second == "-n";
  }

  if(first == "remote") {
    std::string second;
    iss >> second;
    if(second == "-v") {
      std::string extra;
      return !(iss >> extra);
    }
    if(second == "show") {
      std::string no_query_flag;
      iss >> no_query_flag;
      if(no_query_flag != "-n") {
        return false;
      }
      std::string remote_name;
      iss >> remote_name;
      std::string extra;
      return !(iss >> extra);
    }
    return false;
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
    : workspace_root_(normalize_workspace_root(workspace_root)) {}

std::string GitReadTool::name() const {
  return "git";
}

std::string GitReadTool::description() const {
  return "Run read-only git commands (status, log metadata, diff summaries, branch, tag, etc.)";
}

std::string GitReadTool::search_hint() const {
  return "git status log diff branch tag remote rev-parse";
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

  const auto lower_subcommand = ava::core::lowercase_ascii(subcommand);
  if(!is_safe_git_subcommand(lower_subcommand) || contains_mutating_git_patterns(" " + lower_subcommand) ||
     has_forbidden_git_path_or_option(subcommand, workspace_root_) || contains_patch_or_object_output(lower_subcommand)) {
    throw std::runtime_error(
        "git command not allowed in read-only mode: " + subcommand +
        ". Only read-only git commands are permitted: status, log metadata, diff summaries, branch, tag, remote, rev-parse, ls-files, describe, shortlog, stash list."
    );
  }

  const auto outcome = run_shell_command(
      "GIT_OPTIONAL_LOCKS=0 GIT_EXTERNAL_DIFF= GIT_PAGER=cat git "
      "-c protocol.ext.allow=never -c protocol.file.allow=never -c core.askPass= -c core.fsmonitor=false -c gpg.program=false " + subcommand,
      workspace_root_,
      kDefaultShellCommandTimeoutMs
  );
  const auto subcommand_tokens = split_tokens(lower_subcommand);
  const bool is_remote_command = !subcommand_tokens.empty() && subcommand_tokens.front() == "remote";
  const auto content = render_shell_result(CommandOutcome{
      .output = redact_git_remote_credentials(remove_backup_history_lines(outcome.output, workspace_root_), is_remote_command),
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
