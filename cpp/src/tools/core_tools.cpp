#include "ava/tools/core_tools.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#include <sys/wait.h>

#include "ava/tools/output_fallback.hpp"
#include "ava/tools/path_guard.hpp"

namespace ava::tools {

namespace {

constexpr std::size_t kReadDefaultLimit = 2000;
constexpr std::size_t kReadMaxLineLength = 2000;
constexpr std::size_t kGlobMaxResults = 1000;
constexpr std::size_t kGrepMaxMatches = 500;
constexpr std::size_t kBashOutputBytes = 48 * 1024;
std::atomic<std::uint64_t> g_temp_file_counter{0};

[[nodiscard]] std::string shell_single_quote(const std::string& value) {
  std::string escaped;
  escaped.reserve(value.size() + 2);
  escaped.push_back('\'');
  for(const auto ch : value) {
    if(ch == '\'') {
      escaped += "'\\''";
    } else {
      escaped.push_back(ch);
    }
  }
  escaped.push_back('\'');
  return escaped;
}

[[nodiscard]] std::string read_file_text(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if(!file) {
    throw std::runtime_error("Failed to read file: " + path.string());
  }
  std::ostringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

void write_file_text(const std::filesystem::path& path, const std::string& content) {
  if(path.has_parent_path()) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if(ec) {
      throw std::runtime_error("Failed to create parent directories for: " + path.string());
    }
  }

  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  if(!out) {
    throw std::runtime_error("Failed to write file: " + path.string());
  }
  out << content;
}

[[nodiscard]] std::vector<std::string> split_lines(const std::string& content) {
  std::vector<std::string> lines;
  std::stringstream ss(content);
  std::string line;
  while(std::getline(ss, line)) {
    if(line.size() > kReadMaxLineLength) {
      line.resize(kReadMaxLineLength);
    }
    lines.push_back(line);
  }
  if(content.ends_with('\n') && !content.empty()) {
    // keep behavior closer to line-based display (terminal newline)
  }
  return lines;
}

[[nodiscard]] std::regex glob_to_regex(const std::string& glob_pattern) {
  std::string regex = "^";
  for(std::size_t i = 0; i < glob_pattern.size(); ++i) {
    const char c = glob_pattern[i];
    if(c == '*') {
      const bool doublestar = (i + 1 < glob_pattern.size() && glob_pattern[i + 1] == '*');
      if(doublestar) {
        const bool slash_after = (i + 2 < glob_pattern.size() && (glob_pattern[i + 2] == '/' || glob_pattern[i + 2] == '\\'));
        if(slash_after) {
          regex += "(?:.*/)?";
          i += 2;
        } else {
          regex += ".*";
          ++i;
        }
      } else {
        regex += "[^/]*";
      }
    } else if(c == '?') {
      regex += ".";
    } else if(c == '.' || c == '+' || c == '(' || c == ')' || c == '[' || c == ']' || c == '{' || c == '}' ||
              c == '^' || c == '$' || c == '|') {
      regex.push_back('\\');
      regex.push_back(c);
    } else if(c == '\\') {
      regex += "/";
    } else {
      regex.push_back(c);
    }
  }
  regex += "$";
  return std::regex(regex, std::regex::ECMAScript);
}

[[nodiscard]] std::string lowercase(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return value;
}

struct CommandOutcome {
  std::string output;
  int exit_code{1};
};

class TempFileGuard {
 public:
  explicit TempFileGuard(std::filesystem::path path) : path_(std::move(path)) {}

  ~TempFileGuard() {
    if(path_.empty()) {
      return;
    }

    std::error_code ec;
    std::filesystem::remove(path_, ec);
  }

 private:
  std::filesystem::path path_;
};

[[nodiscard]] bool is_path_within_or_equal(
    const std::filesystem::path& root,
    const std::filesystem::path& candidate
) {
  auto root_it = root.begin();
  auto candidate_it = candidate.begin();
  for(; root_it != root.end(); ++root_it, ++candidate_it) {
    if(candidate_it == candidate.end() || *root_it != *candidate_it) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] CommandOutcome run_shell_command(
    const std::string& command,
    const std::filesystem::path& cwd,
    std::uint64_t timeout_ms
) {
  const auto temp_file = std::filesystem::temp_directory_path() /
                         ("ava_tool_output_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) +
                          "_" + std::to_string(g_temp_file_counter.fetch_add(1, std::memory_order_relaxed)) + ".txt");
  TempFileGuard temp_file_guard(temp_file);

  const auto cd_clause = "cd " + shell_single_quote(cwd.string()) + " && ";
  const auto timeout_secs = std::max<std::uint64_t>(1, (timeout_ms + 999) / 1000);
  const auto wrapped = cd_clause + "timeout --signal=TERM --kill-after=1s " + std::to_string(timeout_secs) +
                       "s -- sh -lc " + shell_single_quote(command) + " >" + shell_single_quote(temp_file.string()) +
                       " 2>&1";

  const auto full = "sh -lc " + shell_single_quote(wrapped);
  const int status = std::system(full.c_str());

  std::error_code ec;
  std::string content;
  if(std::filesystem::exists(temp_file, ec) && !ec) {
    content = read_file_text(temp_file);
  }

  int exit_code = 1;
  if(status != -1) {
    if(WIFEXITED(status)) {
      exit_code = WEXITSTATUS(status);
    } else if(WIFSIGNALED(status)) {
      exit_code = 128 + WTERMSIG(status);
    } else {
      exit_code = status;
    }
  }

  return CommandOutcome{.output = std::move(content), .exit_code = exit_code};
}

[[nodiscard]] std::string render_shell_result(const CommandOutcome& outcome) {
  std::ostringstream oss;
  oss << "stdout:\n" << outcome.output << "\n\nstderr:\n\nexit_code: " << outcome.exit_code;
  return oss.str();
}

[[nodiscard]] bool contains_shell_metacharacters(const std::string& command) {
  static constexpr std::array<char, 10> kDisallowed = {';', '|', '&', '$', '<', '>', '`', '(', ')', '\n'};
  return std::any_of(command.begin(), command.end(), [](char ch) {
    return std::find(kDisallowed.begin(), kDisallowed.end(), ch) != kDisallowed.end();
  });
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
    return second.empty() || second == "--list" || second == "-l";
  }

  if(first == "tag") {
    std::string second;
    iss >> second;
    return second.empty() || second == "-l" || second == "--list";
  }

  if(first == "remote") {
    std::string second;
    iss >> second;
    return second == "-v";
  }

  if(first == "stash") {
    std::string second;
    iss >> second;
    return second == "list";
  }

  return false;
}

}  // namespace

ReadTool::ReadTool(std::filesystem::path workspace_root)
    : workspace_root_(normalize_workspace_root(workspace_root)) {}

std::string ReadTool::name() const {
  return "read";
}

std::string ReadTool::description() const {
  return "Read file or directory content";
}

nlohmann::json ReadTool::parameters() const {
  return nlohmann::json{
      {"type", "object"},
      {"required", nlohmann::json::array({"path"})},
      {"properties",
       {
           {"path", {{"type", "string"}}},
           {"offset", {{"type", "integer"}, {"minimum", 1}}},
           {"limit", {{"type", "integer"}, {"minimum", 1}}},
       }},
  };
}

ava::types::ToolResult ReadTool::execute(const nlohmann::json& args) const {
  if(!args.contains("path")) {
    throw std::runtime_error("missing required field: path");
  }

  const auto path = args.at("path").get<std::string>();
  const auto offset = args.value("offset", static_cast<std::size_t>(1));
  const auto limit = args.value("limit", kReadDefaultLimit);
  const auto full_path = enforce_workspace_path(workspace_root_, path, name());

  std::error_code ec;
  if(!std::filesystem::exists(full_path, ec) || ec) {
    throw std::runtime_error("Not found: " + path);
  }

  if(std::filesystem::is_directory(full_path, ec) && !ec) {
    std::vector<std::string> entries;
    for(const auto& entry : std::filesystem::directory_iterator(full_path)) {
      auto display = entry.path().filename().string();
      if(entry.is_directory()) {
        display += "/";
      }
      entries.push_back(display);
    }
    std::sort(entries.begin(), entries.end());
    std::ostringstream out;
    for(std::size_t idx = 0; idx < entries.size(); ++idx) {
      if(idx > 0) {
        out << "\n";
      }
      out << entries[idx];
    }
    return ava::types::ToolResult{.call_id = "", .content = out.str(), .is_error = false};
  }

  const auto content = read_file_text(full_path);
  const auto lines = split_lines(content);

  const std::size_t start = offset > 0 ? offset - 1 : 0;
  std::ostringstream out;
  std::size_t emitted = 0;
  for(std::size_t index = start; index < lines.size() && emitted < limit; ++index, ++emitted) {
    if(emitted > 0) {
      out << "\n";
    }
    out << (index + 1) << ": " << lines[index];
  }

  return ava::types::ToolResult{.call_id = "", .content = out.str(), .is_error = false};
}

WriteTool::WriteTool(std::filesystem::path workspace_root, std::shared_ptr<FileBackupSession> backup_session)
    : workspace_root_(normalize_workspace_root(workspace_root)),
      backup_session_(std::move(backup_session)) {}

std::string WriteTool::name() const {
  return "write";
}

std::string WriteTool::description() const {
  return "Write content to a file";
}

nlohmann::json WriteTool::parameters() const {
  return nlohmann::json{{"type", "object"},
                        {"required", nlohmann::json::array({"path", "content"})},
                        {"properties", {{"path", {{"type", "string"}}}, {"content", {{"type", "string"}}}}}};
}

ava::types::ToolResult WriteTool::execute(const nlohmann::json& args) const {
  if(!args.contains("path") || !args.contains("content")) {
    throw std::runtime_error("missing required fields: path/content");
  }

  const auto path = args.at("path").get<std::string>();
  const auto content = args.at("content").get<std::string>();
  const auto full_path = enforce_workspace_path(workspace_root_, path, name());

  if(backup_session_) {
    backup_session_->backup_file_before_edit(full_path);
  }
  write_file_text(full_path, content);

  return ava::types::ToolResult{
      .call_id = "",
      .content = "Wrote " + std::to_string(content.size()) + " bytes to " + path,
      .is_error = false,
  };
}

EditTool::EditTool(std::filesystem::path workspace_root, std::shared_ptr<FileBackupSession> backup_session)
    : workspace_root_(normalize_workspace_root(workspace_root)),
      backup_session_(std::move(backup_session)) {}

std::string EditTool::name() const {
  return "edit";
}

std::string EditTool::description() const {
  return "Edit existing file content (exact-match strategy for Milestone 6)";
}

nlohmann::json EditTool::parameters() const {
  return nlohmann::json{{"type", "object"},
                        {"required", nlohmann::json::array({"path", "old_text", "new_text"})},
                        {"properties",
                         {{"path", {{"type", "string"}}},
                          {"old_text", {{"type", "string"}}},
                          {"new_text", {{"type", "string"}}},
                          {"replace_all", {{"type", "boolean"}}}}}};
}

ava::types::ToolResult EditTool::execute(const nlohmann::json& args) const {
  if(!args.contains("path") || !args.contains("old_text") || !args.contains("new_text")) {
    throw std::runtime_error("missing required fields: path/old_text/new_text");
  }

  const auto path = args.at("path").get<std::string>();
  const auto old_text = args.at("old_text").get<std::string>();
  const auto new_text = args.at("new_text").get<std::string>();
  const bool replace_all = args.value("replace_all", false);

  if(old_text.empty()) {
    throw std::runtime_error("old_text must not be empty");
  }

  const auto full_path = enforce_workspace_path(workspace_root_, path, name());
  if(backup_session_) {
    backup_session_->backup_file_before_edit(full_path);
  }
  auto content = read_file_text(full_path);

  auto first = content.find(old_text);
  if(first == std::string::npos) {
    throw std::runtime_error("No matching text found for edit");
  }

  std::size_t replacements = 0;
  if(replace_all) {
    std::size_t cursor = 0;
    while((cursor = content.find(old_text, cursor)) != std::string::npos) {
      content.replace(cursor, old_text.size(), new_text);
      cursor += new_text.size();
      ++replacements;
    }
  } else {
    content.replace(first, old_text.size(), new_text);
    replacements = 1;
  }

  write_file_text(full_path, content);

  const auto strategy = replace_all ? "replace_all" : "exact_match";
  return ava::types::ToolResult{
      .call_id = "",
      .content = "Applied " + strategy + "; replacements=" + std::to_string(replacements),
      .is_error = false,
  };
}

BashTool::BashTool(std::filesystem::path workspace_root)
    : workspace_root_(normalize_workspace_root(workspace_root)) {}

std::string BashTool::name() const {
  return "bash";
}

std::string BashTool::description() const {
  return "Execute shell command";
}

nlohmann::json BashTool::parameters() const {
  return nlohmann::json{{"type", "object"},
                        {"required", nlohmann::json::array({"command"})},
                        {"properties",
                         {{"command", {{"type", "string"}}},
                          {"timeout_ms", {{"type", "integer"}, {"minimum", 1}}},
                          {"cwd", {{"type", "string"}}}}}};
}

ava::types::ToolResult BashTool::execute(const nlohmann::json& args) const {
  if(!args.contains("command")) {
    throw std::runtime_error("missing required field: command");
  }

  const auto command = args.at("command").get<std::string>();
  const std::uint64_t timeout_ms = args.value("timeout_ms", static_cast<std::uint64_t>(120000));
  const auto cwd_raw = args.value("cwd", std::string("."));
  const auto cwd = enforce_workspace_path(workspace_root_, cwd_raw, name());

  const auto outcome = run_shell_command(command, cwd, timeout_ms);
  auto content = render_shell_result(outcome);
  content = apply_output_fallback(name(), content, kBashOutputBytes);

  return ava::types::ToolResult{.call_id = "", .content = content, .is_error = outcome.exit_code != 0};
}

GlobTool::GlobTool(std::filesystem::path workspace_root)
    : workspace_root_(normalize_workspace_root(workspace_root)) {}

std::string GlobTool::name() const {
  return "glob";
}

std::string GlobTool::description() const {
  return "Find files by glob pattern";
}

nlohmann::json GlobTool::parameters() const {
  return nlohmann::json{{"type", "object"},
                        {"required", nlohmann::json::array({"pattern"})},
                        {"properties", {{"pattern", {{"type", "string"}}}, {"path", {{"type", "string"}}}}}};
}

ava::types::ToolResult GlobTool::execute(const nlohmann::json& args) const {
  if(!args.contains("pattern")) {
    throw std::runtime_error("missing required field: pattern");
  }

  const auto pattern = args.at("pattern").get<std::string>();
  const auto base = enforce_workspace_path(workspace_root_, args.value("path", std::string(".")), name());
  const auto pattern_re = glob_to_regex(pattern);

  std::vector<std::string> matches;
  std::error_code ec;
  if(!std::filesystem::exists(base, ec) || ec) {
    throw std::runtime_error("Not found: " + base.string());
  }
  if(std::filesystem::is_regular_file(base, ec) && !ec) {
    const auto rel = std::filesystem::relative(base, base.parent_path());
    if(std::regex_match(rel.generic_string(), pattern_re)) {
      matches.push_back(base.string());
    }
  } else {
    for(const auto& entry : std::filesystem::recursive_directory_iterator(base, std::filesystem::directory_options::skip_permission_denied)) {
      if(entry.is_symlink(ec) && !ec) {
        auto symlink_target = std::filesystem::read_symlink(entry.path(), ec);
        if(!ec) {
          if(symlink_target.is_relative()) {
            symlink_target = entry.path().parent_path() / symlink_target;
          }
          const auto resolved_target = std::filesystem::weakly_canonical(symlink_target, ec);
          if(!ec && !is_path_within_or_equal(workspace_root_, resolved_target)) {
            continue;
          }
        }
      }
      const auto rel = std::filesystem::relative(entry.path(), base).generic_string();
      if(std::regex_match(rel, pattern_re)) {
        matches.push_back(entry.path().string());
      }
    }
  }

  std::sort(matches.begin(), matches.end());
  const bool truncated = matches.size() > kGlobMaxResults;
  if(truncated) {
    matches.resize(kGlobMaxResults);
  }

  std::ostringstream out;
  for(std::size_t idx = 0; idx < matches.size(); ++idx) {
    if(idx > 0) {
      out << "\n";
    }
    out << matches[idx];
  }
  if(truncated) {
    out << "\n\n(Results are truncated: showing first " << kGlobMaxResults << " results.)";
  }

  return ava::types::ToolResult{.call_id = "", .content = out.str(), .is_error = false};
}

GrepTool::GrepTool(std::filesystem::path workspace_root)
    : workspace_root_(normalize_workspace_root(workspace_root)) {}

std::string GrepTool::name() const {
  return "grep";
}

std::string GrepTool::description() const {
  return "Search files by regex";
}

nlohmann::json GrepTool::parameters() const {
  return nlohmann::json{{"type", "object"},
                        {"required", nlohmann::json::array({"pattern"})},
                        {"properties",
                         {{"pattern", {{"type", "string"}}},
                          {"path", {{"type", "string"}}},
                          {"include", {{"type", "string"}}}}}};
}

ava::types::ToolResult GrepTool::execute(const nlohmann::json& args) const {
  if(!args.contains("pattern")) {
    throw std::runtime_error("missing required field: pattern");
  }

  const auto pattern = args.at("pattern").get<std::string>();
  std::regex matcher;
  try {
    matcher = std::regex(pattern);
  } catch(const std::regex_error& ex) {
    return ava::types::ToolResult{.call_id = "", .content = std::string("Invalid regex: ") + ex.what(), .is_error = true};
  }
  const auto base = enforce_workspace_path(workspace_root_, args.value("path", std::string(".")), name());
  std::error_code ec;
  if(!std::filesystem::exists(base, ec) || ec) {
    return ava::types::ToolResult{.call_id = "", .content = "Not found: " + base.string(), .is_error = true};
  }

  std::optional<std::regex> include_filter;
  if(args.contains("include")) {
    include_filter = glob_to_regex(args.at("include").get<std::string>());
  }

  std::vector<std::string> matches;
  for(const auto& entry : std::filesystem::recursive_directory_iterator(base, std::filesystem::directory_options::skip_permission_denied)) {
    if(entry.is_symlink(ec) && !ec) {
      continue;
    }
    if(!entry.is_regular_file()) {
      continue;
    }

    const auto relative = std::filesystem::relative(entry.path(), base).generic_string();
    if(include_filter.has_value() && !std::regex_match(relative, include_filter.value())) {
      continue;
    }

    std::ifstream file(entry.path());
    if(!file) {
      continue;
    }

    std::string line;
    std::size_t line_number = 0;
    while(std::getline(file, line)) {
      ++line_number;
      if(line.size() > 8192) {
        line.resize(8192);
      }
      if(std::regex_search(line, matcher)) {
        std::ostringstream hit;
        hit << entry.path().string() << ":" << line_number << ":" << line;
        matches.push_back(hit.str());
        if(matches.size() >= kGrepMaxMatches) {
          break;
        }
      }
    }
    if(matches.size() >= kGrepMaxMatches) {
      break;
    }
  }

  std::sort(matches.begin(), matches.end());
  std::ostringstream out;
  for(std::size_t idx = 0; idx < matches.size(); ++idx) {
    if(idx > 0) {
      out << "\n";
    }
    out << matches[idx];
  }
  if(matches.size() >= kGrepMaxMatches) {
    out << "\n\n(Results truncated: showing first " << kGrepMaxMatches << " matches.)";
  }

  return ava::types::ToolResult{.call_id = "", .content = out.str(), .is_error = false};
}

GitReadTool::GitReadTool(std::filesystem::path workspace_root)
    : workspace_root_(normalize_workspace_root(workspace_root)),
      tool_name_("git") {}

std::string GitReadTool::name() const {
  return tool_name_;
}

std::string GitReadTool::description() const {
  return "Run read-only git commands (status, log, diff, show, blame, etc.)";
}

nlohmann::json GitReadTool::parameters() const {
  return nlohmann::json{{"type", "object"},
                        {"required", nlohmann::json::array({"command"})},
                        {"properties", {{"command", {{"type", "string"}}}}}};
}

ava::types::ToolResult GitReadTool::execute(const nlohmann::json& args) const {
  if(!args.contains("command")) {
    throw std::runtime_error("missing required field: command");
  }

  const auto subcommand = args.at("command").get<std::string>();
  if(contains_shell_metacharacters(subcommand)) {
    throw std::runtime_error("git command contains disallowed shell metacharacters");
  }

  const auto lower_subcommand = lowercase(subcommand);
  if(!is_safe_git_subcommand(lower_subcommand) || contains_mutating_git_patterns(" " + lower_subcommand)) {
    throw std::runtime_error("git command not allowed in read-only mode: " + subcommand);
  }

  const auto outcome = run_shell_command("git " + subcommand, workspace_root_, 120000);
  const auto content = render_shell_result(outcome);

  return ava::types::ToolResult{.call_id = "", .content = content, .is_error = outcome.exit_code != 0};
}

GitReadAliasTool::GitReadAliasTool(std::filesystem::path workspace_root)
    : GitReadTool(std::move(workspace_root)) {}

std::string GitReadAliasTool::name() const {
  return "git_read";
}

DefaultToolRegistration register_default_tools(ToolRegistry& registry, const std::filesystem::path& workspace_root) {
  auto backup_session = std::make_shared<FileBackupSession>(workspace_root);

  registry.register_tool(std::make_unique<ReadTool>(workspace_root));
  registry.register_tool(std::make_unique<WriteTool>(workspace_root, backup_session));
  registry.register_tool(std::make_unique<EditTool>(workspace_root, backup_session));
  registry.register_tool(std::make_unique<BashTool>(workspace_root));
  registry.register_tool(std::make_unique<GlobTool>(workspace_root));
  registry.register_tool(std::make_unique<GrepTool>(workspace_root));
  registry.register_tool(std::make_unique<GitReadTool>(workspace_root));
  registry.register_tool(std::make_unique<GitReadAliasTool>(workspace_root));

  return DefaultToolRegistration{.backup_session = std::move(backup_session)};
}

}  // namespace ava::tools
