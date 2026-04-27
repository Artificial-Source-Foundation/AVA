#include "ava/tools/search_tools.h"

#include <algorithm>
#include <fstream>
#include <regex>
#include <string>
#include <system_error>

namespace ava::tools {

namespace {

ava::core::VoidResult ensure_search_permission(const ToolContext& context) {
  const auto decision = ava::permissions::decide(ava::permissions::PermissionRequest{
      .operation = ava::permissions::Operation::SearchFiles,
      .mode = context.mode,
      .workspace_dir = context.workspace_dir,
      .target_path = context.workspace_dir,
      .command = "",
  });
  if (decision.action == ava::permissions::PermissionAction::Allow) {
    return {};
  }

  auto error = ava::core::Error(ava::core::ErrorCategory::PermissionDenied, "tool requires permission");
  error.with_context("action", ava::permissions::to_string(decision.action));
  error.with_context("reason", decision.reason);
  error.with_context("path", context.workspace_dir.string());
  return std::unexpected(std::move(error));
}

bool is_hidden_or_generated(const std::filesystem::path& path) {
  for (const auto& part : path) {
    const auto name = part.string();
    if (name == ".git" || name == "build" || name == "node_modules" || name == "target" || name == "dist") {
      return true;
    }
  }
  return false;
}

std::string regex_escape(char ch) {
  static const std::string special = R"(\.^$|()[]{}+)";
  if (special.find(ch) != std::string::npos) {
    return std::string("\\") + ch;
  }
  return std::string(1, ch);
}

ava::core::Result<std::regex> glob_to_regex(std::string_view pattern) {
  std::string out = "^";
  for (std::size_t i = 0; i < pattern.size(); ++i) {
    const char ch = pattern[i];
    if (ch == '*') {
      if (i + 1 < pattern.size() && pattern[i + 1] == '*') {
        if (i + 2 < pattern.size() && pattern[i + 2] == '/') {
          out += "(?:.*/)?";
          i += 2;
        } else {
          out += ".*";
          ++i;
        }
      } else {
        out += "[^/]*";
      }
    } else if (ch == '?') {
      out += "[^/]";
    } else {
      out += regex_escape(ch);
    }
  }
  out += "$";

  try {
    return std::regex(out, std::regex::ECMAScript);
  } catch (const std::regex_error& err) {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "invalid glob pattern");
    error.with_context("pattern", std::string(pattern));
    error.with_context("cause", err.what());
    return std::unexpected(std::move(error));
  }
}

std::string relative_slash_path(const std::filesystem::path& root, const std::filesystem::path& path) {
  std::error_code error;
  auto relative = std::filesystem::relative(path, root, error);
  if (error) {
    relative = path.filename();
  }
  return relative.generic_string();
}

bool looks_binary(std::string_view line) {
  return line.find('\0') != std::string_view::npos;
}

bool can_read_search_match(const ToolContext& context, const std::filesystem::path& path) {
  const auto decision = ava::permissions::decide(ava::permissions::PermissionRequest{
      .operation = ava::permissions::Operation::ReadFile,
      .mode = context.mode,
      .workspace_dir = context.workspace_dir,
      .target_path = path,
      .command = "",
  });
  return decision.action == ava::permissions::PermissionAction::Allow;
}

ava::core::Result<bool> read_limited_line(std::ifstream& file,
                                          std::string& line,
                                          const std::filesystem::path& path,
                                          std::size_t max_line_length,
                                          bool& line_truncated) {
  line.clear();
  line_truncated = false;
  bool saw_character = false;
  char ch = '\0';
  while (file.get(ch)) {
    saw_character = true;
    if (ch == '\n') {
      return true;
    }
    if (line.size() < max_line_length) {
      line.push_back(ch);
    } else {
      line_truncated = true;
    }
  }
  if (!file.eof() && file.fail()) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed while reading file");
    error.with_context("path", path.string());
    return std::unexpected(std::move(error));
  }
  return saw_character;
}

}  // namespace

ava::core::Result<GlobResult> glob_files(const ToolContext& context,
                                         std::string_view pattern,
                                         GlobOptions options) {
  if (pattern.empty()) {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "glob pattern must not be empty");
    return std::unexpected(std::move(error));
  }
  if (auto permission = ensure_search_permission(context); !permission) {
    return std::unexpected(permission.error());
  }

  auto matcher = glob_to_regex(pattern);
  if (!matcher) {
    return std::unexpected(matcher.error());
  }

  GlobResult result;
  std::size_t visited = 0;
  std::error_code iter_error;
  for (std::filesystem::recursive_directory_iterator it(context.workspace_dir, iter_error), end; it != end;
       it.increment(iter_error)) {
    if (iter_error) {
      iter_error.clear();
      continue;
    }
    const auto& entry = *it;
    ++visited;
    if (visited > options.max_visited) {
      result.truncated = true;
      break;
    }
    if (static_cast<std::size_t>(it.depth()) > options.max_depth) {
      if (entry.is_directory(iter_error)) {
        it.disable_recursion_pending();
      }
      continue;
    }
    if (entry.is_directory(iter_error) && is_hidden_or_generated(entry.path())) {
      it.disable_recursion_pending();
      continue;
    }
    if (!entry.is_regular_file(iter_error)) {
      continue;
    }

    const auto relative = relative_slash_path(context.workspace_dir, entry.path());
    bool matched = false;
    try {
      matched = std::regex_match(relative, *matcher);
    } catch (const std::regex_error& err) {
      auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "glob regex match failed");
      error.with_context("pattern", std::string(pattern));
      error.with_context("cause", err.what());
      return std::unexpected(std::move(error));
    }
    if (!matched) {
      continue;
    }

    if (!can_read_search_match(context, entry.path())) {
      continue;
    }
    ++result.total_matches;
    if (result.paths.size() < options.max_results) {
      result.paths.push_back(entry.path());
    } else {
      result.truncated = true;
    }
  }
  std::ranges::sort(result.paths);
  return result;
}

ava::core::Result<GrepResult> grep_files(const ToolContext& context,
                                         std::string_view literal_pattern,
                                         std::string_view include_glob,
                                         GrepOptions options) {
  if (literal_pattern.empty()) {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "grep pattern must not be empty");
    return std::unexpected(std::move(error));
  }

  auto files = glob_files(context, include_glob, GlobOptions{.max_results = 100000});
  if (!files) {
    return std::unexpected(files.error());
  }

  GrepResult result;
  result.truncated = files->truncated;
  for (const auto& path : files->paths) {
    if (!can_read_search_match(context, path)) {
      continue;
    }

    std::ifstream file(path, std::ios::binary);
    if (!file) {
      continue;
    }

    std::string line;
    std::size_t line_number = 0;
    while (true) {
      bool line_truncated = false;
      auto line_read = read_limited_line(file, line, path, options.max_line_length, line_truncated);
      if (!line_read) {
        return std::unexpected(line_read.error());
      }
      if (!*line_read) {
        break;
      }
      ++line_number;
      const bool matched = line.find(literal_pattern) != std::string::npos;
      if (looks_binary(line) || !matched) {
        continue;
      }

      ++result.total_matches;
      if (result.matches.size() >= options.max_matches) {
        result.truncated = true;
        continue;
      }

      result.matches.push_back(GrepMatch{
          .path = path,
          .line_number = line_number,
          .line = line,
          .line_truncated = line_truncated,
      });
    }
  }

  return result;
}

}  // namespace ava::tools
