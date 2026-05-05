#include "ava/tools/search_tools_support.h"

#include <utility>

namespace ava::tools::detail {

std::string search_regex_escape(char ch)
{
  static std::string const special = R"(\.^$|()[]{}+)";
  if (special.find(ch) != std::string::npos) {
    return std::string("\\") + ch;
  }
  return std::string(1, ch);
}

ava::core::Result<std::regex> search_glob_to_regex(std::string_view pattern)
{
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
      out += search_regex_escape(ch);
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

std::string search_relative_slash_path(std::filesystem::path const& root, std::filesystem::path const& path)
{
  std::error_code error;
  auto relative = std::filesystem::relative(path, root, error);
  if (error) {
    relative = path.filename();
  }
  return relative.generic_string();
}

bool search_looks_binary(std::string_view line)
{
  return line.find('\0') != std::string_view::npos;
}

std::string search_permission_tool_name(ToolContext const& context)
{
  return context.permission_tool_name.empty() ? std::string("search") : context.permission_tool_name;
}

bool is_search_canceled(ToolContext const& context)
{
  return context.cancel_requested && context.cancel_requested();
}

ava::core::Error search_canceled_error(std::string_view tool_name)
{
  auto error = ava::core::Error(ava::core::ErrorCategory::Unknown, "tool canceled");
  error.with_context("tool", std::string(tool_name));
  return error;
}

ava::core::VoidResult check_search_canceled(ToolContext const& context, std::string_view tool_name)
{
  if (!is_search_canceled(context)) return {};
  return std::unexpected(search_canceled_error(tool_name));
}

ava::core::Result<bool> can_read_search_match(ToolContext const& context, std::filesystem::path const& path)
{
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

ava::core::Result<bool> read_limited_search_line(std::ifstream& file, std::string& line,
                                                 std::filesystem::path const& path, std::size_t max_line_length,
                                                 bool& line_truncated, bool& line_binary)
{
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

}  // namespace ava::tools::detail
