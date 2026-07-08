#include "sys.h"
#include "ava/tools/ignore_rules.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <string_view>
#include <system_error>
#include <utility>

namespace ava::tools {

namespace {

constexpr std::size_t kMaxIgnoreRuleWalkEntries = 100000;

bool is_blank(std::string_view value)
{
  return std::ranges::all_of(value, [](char ch) { return std::isspace(static_cast<unsigned char>(ch)) != 0; });
}

bool is_escaped_trailing_space(std::string_view value, std::size_t space_index)
{
  std::size_t backslash_count = 0;
  for (std::size_t index = space_index; index > 0 && value[index - 1] == '\\'; --index)
  {
    ++backslash_count;
  }
  return backslash_count % 2 == 1;
}

void strip_unescaped_trailing_spaces(std::string& line)
{
  while (!line.empty() && line.back() == ' ' && !is_escaped_trailing_space(line, line.size() - 1))
  {
    line.pop_back();
  }
}

std::string regex_escape(char ch)
{
  static std::string const special = R"(\.^$|()[]{}+*?)";
  if (special.find(ch) != std::string::npos)
    return std::string("\\") + ch;
  return std::string(1, ch);
}

std::string wildcard_to_regex(std::string_view pattern)
{
  std::string out = "^";
  for (std::size_t index = 0; index < pattern.size(); ++index)
  {
    char const ch = pattern[index];
    if (ch == '\\' && index + 1 < pattern.size())
    {
      out += regex_escape(pattern[index + 1]);
      ++index;
    }
    else if (ch == '*')
    {
      if (index + 1 < pattern.size() && pattern[index + 1] == '*')
      {
        if (index + 2 < pattern.size() && pattern[index + 2] == '/')
        {
          out += "(?:.*/)?";
          index += 2;
        }
        else
        {
          out += ".*";
          ++index;
        }
      }
      else
      {
        out += "[^/]*";
      }
    }
    else if (ch == '?')
    {
      out += "[^/]";
    }
    else
    {
      out += regex_escape(ch);
    }
  }
  out += "$";
  return out;
}

std::vector<std::string_view> slash_components(std::string_view path)
{
  std::vector<std::string_view> components;
  std::size_t start = 0;
  while (start <= path.size())
  {
    auto const slash = path.find('/', start);
    auto const end = slash == std::string_view::npos ? path.size() : slash;
    if (end > start)
      components.push_back(path.substr(start, end - start));
    if (slash == std::string_view::npos)
      break;
    start = slash + 1;
  }
  return components;
}

std::vector<std::string> ancestor_dirs(std::string_view path, bool is_directory)
{
  std::vector<std::string> ancestors;
  auto const components = slash_components(path);
  auto const count = is_directory ? components.size() : (components.empty() ? 0 : components.size() - 1);
  std::string current;
  for (std::size_t index = 0; index < count; ++index)
  {
    if (!current.empty())
      current += '/';
    current += components[index];
    ancestors.push_back(current);
  }
  return ancestors;
}

bool starts_with_path_prefix(std::string_view value, std::string_view prefix)
{
  if (prefix.empty())
    return true;
  return value == prefix || (value.size() > prefix.size() && value.starts_with(prefix) && value[prefix.size()] == '/');
}

}  // namespace

bool is_git_dir(std::filesystem::path const& path)
{
  for (auto const& part : path)
  {
    if (part == ".git")
      return true;
  }
  return false;
}

bool is_generated_dir(std::filesystem::path const& path)
{
  auto const name = path.filename().string();
  return name == "build" || name == "node_modules" || name == "target" || name == "dist";
}

IgnoreMatcher::IgnoreMatcher(std::filesystem::path workspace_dir) : workspace_dir_(std::move(workspace_dir))
{
}

ava::core::Result<IgnoreMatcher> IgnoreMatcher::load(std::filesystem::path const& workspace_dir)
{
  IgnoreMatcher matcher(workspace_dir);
  auto loaded = matcher.load_rules();
  if (!loaded)
    return std::unexpected(loaded.error());
  return matcher;
}

bool IgnoreMatcher::ignored(std::filesystem::path const& path, bool is_directory) const
{
  auto const relative = relative_to_workspace(path);
  bool ignored = false;
  for (auto const& rule : rules_)
  {
    if (!starts_with_path_prefix(relative, rule.base_relative))
      continue;
    std::string_view relative_to_base = relative;
    if (!rule.base_relative.empty())
    {
      relative_to_base.remove_prefix(rule.base_relative.size());
      if (!relative_to_base.empty() && relative_to_base.front() == '/')
        relative_to_base.remove_prefix(1);
    }
    if (relative_to_base.empty())
      continue;
    if (rule_matches(rule, relative_to_base, is_directory))
      ignored = !rule.negated;
  }
  return ignored;
}

bool IgnoreMatcher::rule_matches(Rule const& rule, std::string_view relative_to_base, bool is_directory)
{
  if (rule.anchored || rule.contains_slash)
  {
    if (rule.directory_only)
    {
      for (auto const& ancestor : ancestor_dirs(relative_to_base, is_directory))
      {
        if (std::regex_match(ancestor, rule.matcher))
          return true;
      }
      return false;
    }
    return std::regex_match(std::string(relative_to_base), rule.matcher);
  }

  auto const components = slash_components(relative_to_base);
  auto const limit = rule.directory_only && !is_directory && !components.empty() ? components.size() - 1 : components.size();
  for (std::size_t index = 0; index < limit; ++index)
  {
    if (std::regex_match(std::string(components[index]), rule.matcher))
      return true;
  }
  return false;
}

ava::core::Result<void> IgnoreMatcher::load_rules()
{
  auto root_loaded = load_file(workspace_dir_ / ".gitignore");
  if (!root_loaded)
    return std::unexpected(root_loaded.error());

  std::error_code iter_error;
  std::size_t visited = 0;
  for (std::filesystem::recursive_directory_iterator it(workspace_dir_, iter_error), end; it != end; it.increment(iter_error))
  {
    if (iter_error)
    {
      iter_error.clear();
      continue;
    }
    ++visited;
    if (visited > kMaxIgnoreRuleWalkEntries)
    {
      auto error = ava::core::Error(ava::core::ErrorCategory::Tool, "too many files while loading .gitignore rules");
      error.with_context("workspace", workspace_dir_.string());
      error.with_context("max_visited", std::to_string(kMaxIgnoreRuleWalkEntries));
      return std::unexpected(std::move(error));
    }
    auto const& entry = *it;
    if (entry.is_directory(iter_error))
    {
      if (is_git_dir(entry.path()) || is_generated_dir(entry.path()))
      {
        it.disable_recursion_pending();
      }
      continue;
    }
    if (!entry.is_regular_file(iter_error) || entry.path().filename() != ".gitignore" || entry.path().parent_path() == workspace_dir_)
    {
      continue;
    }
    auto loaded = load_file(entry.path());
    if (!loaded)
      return std::unexpected(loaded.error());
  }
  return {};
}

ava::core::Result<void> IgnoreMatcher::load_file(std::filesystem::path const& ignore_file)
{
  std::error_code status_error;
  auto const status = std::filesystem::symlink_status(ignore_file, status_error);
  if (status_error || std::filesystem::is_symlink(status))
    return {};

  std::ifstream file(ignore_file, std::ios::binary);
  if (!file)
    return {};

  auto const base_relative = relative_to_workspace(ignore_file.parent_path());
  std::string line;
  while (std::getline(file, line))
  {
    if (!line.empty() && line.back() == '\r')
      line.pop_back();
    strip_unescaped_trailing_spaces(line);
    if (line.empty() || is_blank(line) || line.front() == '#')
      continue;

    Rule rule;
    rule.base_relative = base_relative == "." ? std::string{} : base_relative;
    if (line.front() == '!')
    {
      rule.negated = true;
      line.erase(line.begin());
      if (line.empty())
        continue;
    }
    if (line.front() == '/')
    {
      rule.anchored = true;
      line.erase(line.begin());
      if (line.empty())
        continue;
    }
    while (!line.empty() && line.back() == '/')
    {
      rule.directory_only = true;
      line.pop_back();
    }
    if (line.empty())
      continue;

    rule.pattern = line;
    if (rule.pattern.empty())
      continue;
    rule.contains_slash = rule.pattern.find('/') != std::string::npos;
    try
    {
      rule.matcher = std::regex(wildcard_to_regex(rule.pattern), std::regex::ECMAScript);
    }
    catch (std::regex_error const& err)
    {
      auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "invalid .gitignore pattern");
      error.with_context("path", ignore_file.string());
      error.with_context("pattern", rule.pattern);
      error.with_context("cause", err.what());
      return std::unexpected(std::move(error));
    }
    rules_.push_back(std::move(rule));
  }

  if (!file.eof() && file.fail())
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed while reading .gitignore");
    error.with_context("path", ignore_file.string());
    return std::unexpected(std::move(error));
  }
  return {};
}

std::string IgnoreMatcher::relative_to_workspace(std::filesystem::path const& path) const
{
  std::error_code error;
  auto relative = std::filesystem::relative(path, workspace_dir_, error);
  if (error)
    return {};
  return relative.generic_string();
}

}  // namespace ava::tools
