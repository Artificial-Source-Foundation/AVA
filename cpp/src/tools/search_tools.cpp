#include "ava/tools/search_tools.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#include <nlohmann/json.hpp>

#include "ava/tools/file_backup.hpp"
#include "ava/tools/path_guard.hpp"

namespace ava::tools {
namespace {

constexpr std::size_t kGlobMaxResults = 1000;
constexpr std::size_t kGrepMaxMatches = 500;

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

[[nodiscard]] bool is_safe_symlink_entry(
    const std::filesystem::path& workspace_root,
    const std::filesystem::directory_entry& entry,
    std::error_code& ec
) {
  if(!entry.is_symlink(ec) || ec) {
    return true;
  }
  auto symlink_target = std::filesystem::read_symlink(entry.path(), ec);
  if(ec) {
    return false;
  }
  if(symlink_target.is_relative()) {
    symlink_target = entry.path().parent_path() / symlink_target;
  }
  const auto resolved_target = std::filesystem::weakly_canonical(symlink_target, ec);
  return !ec && is_path_within_or_equal(workspace_root, resolved_target);
}

}  // namespace

GlobTool::GlobTool(std::filesystem::path workspace_root)
    : workspace_root_(normalize_workspace_root(workspace_root)) {}

std::string GlobTool::name() const {
  return "glob";
}

std::string GlobTool::description() const {
  return "Find files by glob pattern";
}

std::string GlobTool::search_hint() const {
  return "glob find files pattern path";
}

nlohmann::json GlobTool::parameters() const {
  return nlohmann::json{{"type", "object"},
                        {"required", nlohmann::json::array({"pattern"})},
                        {"properties",
                         {{"pattern", {{"type", "string"}, {"description", "Glob pattern to match"}}},
                          {"path", {{"type", "string"}, {"description", "Directory to search, relative to the workspace root"}}}}}};
}

ava::types::ToolResult GlobTool::execute(const nlohmann::json& args) const {
  if(!args.contains("pattern")) {
    throw std::runtime_error("missing required field: pattern");
  }

  const auto pattern = args.at("pattern").get<std::string>();
  const auto base = enforce_workspace_path(workspace_root_, args.value("path", std::string(".")), name());
  reject_backup_history_access(workspace_root_, base, name());
  const auto pattern_re = glob_to_regex(pattern);

  std::vector<std::string> matches;
  std::error_code ec;
  if(!std::filesystem::exists(base, ec) || ec) {
    throw std::runtime_error("Not found: " + base.string());
  }
  if(std::filesystem::is_regular_file(base, ec) && !ec) {
    const auto rel = std::filesystem::relative(base, base.parent_path());
    if(std::regex_match(rel.generic_string(), pattern_re) && !is_backup_history_path(workspace_root_, base)) {
      matches.push_back(base.string());
    }
  } else {
    for(const auto& entry : std::filesystem::recursive_directory_iterator(base, std::filesystem::directory_options::skip_permission_denied)) {
      if(!is_safe_symlink_entry(workspace_root_, entry, ec)) {
        continue;
      }
      if(!entry.is_regular_file(ec) || ec || is_backup_history_path(workspace_root_, entry.path())) {
        continue;
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
    out << "\n\n(Results are truncated: showing first " << kGlobMaxResults
        << " results. Consider using a more specific path or pattern.)";
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

std::string GrepTool::search_hint() const {
  return "grep search regex pattern files include path";
}

nlohmann::json GrepTool::parameters() const {
  return nlohmann::json{{"type", "object"},
                        {"required", nlohmann::json::array({"pattern"})},
                        {"properties",
                         {{"pattern", {{"type", "string"}, {"description", "Regular expression to search for"}}},
                          {"path", {{"type", "string"}, {"description", "Directory to search, relative to the workspace root"}}},
                          {"include", {{"type", "string"}, {"description", "Optional glob pattern limiting searched files"}}}}}};
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
  reject_backup_history_access(workspace_root_, base, name());
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
    if(!is_safe_symlink_entry(workspace_root_, entry, ec)) {
      continue;
    }
    if(!entry.is_regular_file(ec) || ec || is_backup_history_path(workspace_root_, entry.path())) {
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
    out << "\n\n(Results truncated: showing first " << kGrepMaxMatches
        << " matches. Consider using a more specific path or pattern.)";
  }

  return ava::types::ToolResult{.call_id = "", .content = out.str(), .is_error = false};
}

}  // namespace ava::tools
