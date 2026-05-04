#include "ava/tools/search_tools.h"

#include <algorithm>
#include <fstream>
#include <optional>
#include <regex>
#include <string>
#include <system_error>
#include <utility>

#include "ava/tools/ignore_rules.h"
#include "ava/tools/spill_files.h"

namespace ava::tools {

namespace {

constexpr std::size_t kSearchVisitedProgressInterval = 10000;
constexpr std::size_t kSearchMatchProgressInterval = 500;

std::string regex_escape(char ch) {
  static std::string const special = R"(\.^$|()[]{}+)";
  if (special.find(ch) != std::string::npos) {
    return std::string("\\") + ch;
  }
  return std::string(1, ch);
}

ava::core::Result<std::regex> glob_to_regex(std::string_view pattern) {
  if (pattern.find('[') != std::string_view::npos || pattern.find(']') != std::string_view::npos) {
    auto error =
        ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "glob bracket character classes are not supported");
    error.with_context("pattern", std::string(pattern));
    return std::unexpected(std::move(error));
  }

  std::string out = "^";
  for (std::size_t i = 0; i < pattern.size(); ++i) {
    char const ch = pattern[i];
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
  } catch (std::regex_error const& err) {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "invalid glob pattern");
    error.with_context("pattern", std::string(pattern));
    error.with_context("cause", err.what());
    return std::unexpected(std::move(error));
  }
}

std::string relative_slash_path(std::filesystem::path const& root, std::filesystem::path const& path) {
  std::error_code error;
  auto relative = std::filesystem::relative(path, root, error);
  if (error) {
    relative = path.filename();
  }
  return relative.generic_string();
}

bool looks_binary(std::string_view line) { return line.find('\0') != std::string_view::npos; }

std::string search_permission_tool_name(ToolContext const& context) {
  return context.permission_tool_name.empty() ? std::string("search") : context.permission_tool_name;
}

bool is_canceled(ToolContext const& context) { return context.cancel_requested && context.cancel_requested(); }

ava::core::Error search_canceled_error(std::string_view tool_name) {
  auto error = ava::core::Error(ava::core::ErrorCategory::Unknown, "tool canceled");
  error.with_context("tool", std::string(tool_name));
  return error;
}

ava::core::VoidResult check_canceled(ToolContext const& context, std::string_view tool_name) {
  if (!is_canceled(context)) return {};
  return std::unexpected(search_canceled_error(tool_name));
}

ava::core::Result<bool> can_read_search_match(ToolContext const& context, std::filesystem::path const& path) {
  auto const decision = ava::permissions::decide(ava::permissions::PermissionRequest{
      .operation = ava::permissions::Operation::ReadFile,
      .mode = context.mode,
      .workspace_dir = context.workspace_dir,
      .target_path = path,
      .command = "",
  });
  if (decision.action == ava::permissions::PermissionAction::Allow) return true;
  if (decision.action == ava::permissions::PermissionAction::Deny) return false;

  auto permission = ensure_permission(context, ava::permissions::Operation::ReadFile, path, "",
                                      search_permission_tool_name(context), "search match requires permission");
  if (permission) return true;
  // Search is best-effort per matched file: if an Ask resolver or audit sink fails,
  // skip only this match instead of failing the whole glob/grep operation.
  return false;
}

ava::core::Result<bool> read_limited_line(std::ifstream& file, std::string& line, std::filesystem::path const& path,
                                          std::size_t max_line_length, bool& line_truncated, bool& line_binary) {
  line.clear();
  line_truncated = false;
  line_binary = false;
  bool saw_character = false;
  char ch = '\0';
  while (file.get(ch)) {
    saw_character = true;
    if (ch == '\n') {
      return true;
    }
    if (ch == '\0') {
      line_binary = true;
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

ava::core::Result<GlobResult> glob_files(ToolContext const& context, std::string_view pattern, GlobOptions options) {
  if (pattern.empty()) {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "glob pattern must not be empty");
    return std::unexpected(std::move(error));
  }
  if (auto canceled = check_canceled(context, "glob"); !canceled) return std::unexpected(std::move(canceled.error()));
  auto const tool_name = context.permission_tool_name.empty() ? std::string("search") : context.permission_tool_name;
  if (auto permission = ensure_permission(context, ava::permissions::Operation::SearchFiles, context.workspace_dir, "",
                                          tool_name, "tool requires permission");
      !permission) {
    return std::unexpected(permission.error());
  }

  auto matcher = glob_to_regex(pattern);
  if (!matcher) {
    return std::unexpected(matcher.error());
  }
  std::optional<IgnoreMatcher> ignore_matcher;
  if (!options.no_ignore) {
    auto loaded_ignore_matcher = IgnoreMatcher::load(context.workspace_dir);
    if (!loaded_ignore_matcher) return std::unexpected(loaded_ignore_matcher.error());
    ignore_matcher = std::move(*loaded_ignore_matcher);
  }

  GlobResult result;
  SpillBuffer spill_buffer;
  std::size_t visited = 0;
  std::size_t next_visited_progress = kSearchVisitedProgressInterval;
  std::size_t next_match_progress = kSearchMatchProgressInterval;
  std::error_code iter_error;
  for (std::filesystem::recursive_directory_iterator it(context.workspace_dir, iter_error), end; it != end;
       it.increment(iter_error)) {
    if (auto canceled = check_canceled(context, "glob"); !canceled) return std::unexpected(std::move(canceled.error()));
    if (iter_error) {
      iter_error.clear();
      continue;
    }
    auto const& entry = *it;
    ++visited;
    if (visited >= next_visited_progress) {
      if (auto progress = emit_tool_progress(context, "glob visited " + std::to_string(visited) + " paths", "running");
          !progress) {
        return std::unexpected(std::move(progress.error()));
      }
      next_visited_progress += kSearchVisitedProgressInterval;
    }
    if (visited > options.max_visited) {
      result.truncated = true;
      if (auto progress = emit_tool_progress(
              context, "glob truncated after visiting " + std::to_string(visited) + " paths", "running");
          !progress) {
        return std::unexpected(std::move(progress.error()));
      }
      break;
    }
    if (static_cast<std::size_t>(it.depth()) > options.max_depth) {
      if (entry.is_directory(iter_error)) {
        it.disable_recursion_pending();
      }
      continue;
    }
    if (entry.is_directory(iter_error)) {
      if (is_git_dir(entry.path()) || (!options.no_ignore && is_generated_dir(entry.path()))) {
        it.disable_recursion_pending();
        continue;
      }
      if (ignore_matcher && ignore_matcher->ignored(entry.path(), true)) {
        it.disable_recursion_pending();
        continue;
      }
      continue;
    }
    if (!entry.is_regular_file(iter_error)) {
      continue;
    }

    if (ignore_matcher && ignore_matcher->ignored(entry.path(), false)) {
      continue;
    }

    auto const relative = relative_slash_path(context.workspace_dir, entry.path());
    bool matched = false;
    try {
      matched = std::regex_match(relative, *matcher);
    } catch (std::regex_error const& err) {
      auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "glob regex match failed");
      error.with_context("pattern", std::string(pattern));
      error.with_context("cause", err.what());
      return std::unexpected(std::move(error));
    }
    if (!matched) {
      continue;
    }

    auto can_read = can_read_search_match(context, entry.path());
    if (!can_read) return std::unexpected(can_read.error());
    if (!*can_read) {
      continue;
    }
    ++result.total_matches;
    spill_buffer.append(entry.path().generic_string());
    spill_buffer.append("\n");
    if (result.total_matches >= next_match_progress) {
      if (auto progress =
              emit_tool_progress(context, "glob matched " + std::to_string(result.total_matches) + " paths", "running");
          !progress) {
        return std::unexpected(std::move(progress.error()));
      }
      next_match_progress += kSearchMatchProgressInterval;
    }
    if (result.paths.size() < options.max_results) {
      result.paths.push_back(entry.path());
    } else {
      result.truncated = true;
    }
  }
  std::ranges::sort(result.paths, {}, [](std::filesystem::path const& path) { return path.generic_string(); });
  if (result.truncated && !context.spill_dir.empty()) {
    auto spill = write_spill_file(context, "glob", "txt", spill_buffer);
    if (!spill) return std::unexpected(std::move(spill.error()));
    result.spill_path = spill->path;
    result.spill_truncated = spill->truncated;
    if (auto progress = emit_tool_progress(
            context, "glob results spilled " + std::to_string(spill->bytes_written) + " bytes", "running");
        !progress) {
      return std::unexpected(std::move(progress.error()));
    }
  }
  if (result.truncated || result.total_matches > 0 || visited > 0) {
    if (auto progress = emit_tool_progress(
            context, "glob completed with " + std::to_string(result.total_matches) + " matches", "completed");
        !progress) {
      return std::unexpected(std::move(progress.error()));
    }
  }
  return result;
}

ava::core::Result<GrepResult> grep_files(ToolContext const& context, std::string_view literal_pattern,
                                         std::string_view include_glob, GrepOptions options) {
  if (literal_pattern.empty()) {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "grep pattern must not be empty");
    return std::unexpected(std::move(error));
  }
  if (auto canceled = check_canceled(context, "grep"); !canceled) return std::unexpected(std::move(canceled.error()));

  auto files = glob_files(context, include_glob, GlobOptions{.max_results = 100000, .no_ignore = options.no_ignore});
  if (!files) {
    return std::unexpected(files.error());
  }
  if (auto canceled = check_canceled(context, "grep"); !canceled) return std::unexpected(std::move(canceled.error()));

  GrepResult result;
  result.truncated = files->truncated;
  SpillBuffer spill_buffer;
  std::size_t next_match_progress = kSearchMatchProgressInterval;
  for (auto const& path : files->paths) {
    if (auto canceled = check_canceled(context, "grep"); !canceled) return std::unexpected(std::move(canceled.error()));
    std::ifstream file(path, std::ios::binary);
    if (!file) {
      continue;
    }

    std::string line;
    std::size_t line_number = 0;
    std::size_t file_total_matches = 0;
    std::vector<GrepMatch> file_matches;
    file_matches.reserve(std::min<std::size_t>(options.max_matches, 64));
    SpillBuffer file_spill_buffer;
    bool file_is_binary = false;
    while (true) {
      if (auto canceled = check_canceled(context, "grep"); !canceled)
        return std::unexpected(std::move(canceled.error()));
      bool line_truncated = false;
      bool line_binary = false;
      auto line_read = read_limited_line(file, line, path, options.max_line_length, line_truncated, line_binary);
      if (!line_read) {
        return std::unexpected(line_read.error());
      }
      if (!*line_read) {
        break;
      }
      ++line_number;
      if (line_binary || looks_binary(line)) {
        file_is_binary = true;
        break;
      }
      bool const matched = line.find(literal_pattern) != std::string::npos;
      if (!matched) {
        continue;
      }

      ++file_total_matches;
      file_spill_buffer.append(path.generic_string());
      file_spill_buffer.append(":");
      file_spill_buffer.append(std::to_string(line_number));
      file_spill_buffer.append(":");
      file_spill_buffer.append(line);
      file_spill_buffer.append("\n");
      if (result.total_matches + file_total_matches >= next_match_progress) {
        if (auto progress = emit_tool_progress(
                context, "grep matched " + std::to_string(result.total_matches + file_total_matches) + " lines",
                "running");
            !progress) {
          return std::unexpected(std::move(progress.error()));
        }
        next_match_progress += kSearchMatchProgressInterval;
      }
      if (file_matches.size() >= options.max_matches) continue;

      file_matches.push_back(GrepMatch{
          .path = path,
          .line_number = line_number,
          .line = line,
          .line_truncated = line_truncated,
      });
    }

    if (file_is_binary) {
      continue;
    }

    spill_buffer.append(file_spill_buffer.content());

    if (result.total_matches + file_total_matches > options.max_matches) {
      result.truncated = true;
    }
    result.total_matches += file_total_matches;
    for (auto& match : file_matches) {
      if (result.matches.size() >= options.max_matches) {
        result.truncated = true;
        break;
      }
      result.matches.push_back(std::move(match));
    }
  }

  if (result.truncated && !context.spill_dir.empty()) {
    auto spill = write_spill_file(context, "grep", "txt", spill_buffer);
    if (!spill) return std::unexpected(std::move(spill.error()));
    result.spill_path = spill->path;
    result.spill_truncated = spill->truncated;
    if (auto progress = emit_tool_progress(
            context, "grep results spilled " + std::to_string(spill->bytes_written) + " bytes", "running");
        !progress) {
      return std::unexpected(std::move(progress.error()));
    }
  }
  if (result.truncated || result.total_matches > 0 || !files->paths.empty()) {
    if (auto progress = emit_tool_progress(
            context, "grep completed with " + std::to_string(result.total_matches) + " matches", "completed");
        !progress) {
      return std::unexpected(std::move(progress.error()));
    }
  }

  return result;
}

}  // namespace ava::tools
