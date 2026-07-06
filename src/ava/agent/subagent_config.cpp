#include "ava/agent/subagent_config.h"
#include "ava/config/xdg_paths.h"
#include "ava/core/json.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <map>
#include <optional>
#include <string_view>
#include <system_error>
#include <utility>

namespace ava::agent {
namespace {

constexpr std::size_t kMaxSubagentNameBytes = 128;
constexpr std::size_t kMaxSubagentDescriptionBytes = 1024;
constexpr std::size_t kMaxSubagentPromptBytes = 64 * 1024;
constexpr std::size_t kMaxSubagents = 128;

struct ParsedMarkdown
{
  std::map<std::string, std::string> frontmatter;
  std::string body;
};

std::optional<std::filesystem::path> home_dir()
{
  if (auto const* home = std::getenv("HOME"); home != nullptr && *home != '\0')
    return std::filesystem::path(home);
  return std::nullopt;
}

std::string_view trim_view(std::string_view value)
{
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) value.remove_prefix(1);
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) value.remove_suffix(1);
  return value;
}

std::string trim(std::string_view value)
{
  return std::string(trim_view(value));
}

std::string strip_matching_quotes(std::string value)
{
  if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"') || (value.front() == '\'' && value.back() == '\'')))
    return value.substr(1, value.size() - 2);
  return value;
}

bool has_control_byte(std::string_view value)
{
  return std::ranges::any_of(value, [](char ch) {
    auto const byte = static_cast<unsigned char>(ch);
    return byte < 0x20 && ch != '\n' && ch != '\r' && ch != '\t';
  });
}

std::string xml_escape(std::string_view value)
{
  std::string escaped;
  escaped.reserve(value.size());
  for (char const ch : value)
  {
    switch (ch)
    {
      case '&':
        escaped += "&amp;";
        break;
      case '<':
        escaped += "&lt;";
        break;
      case '>':
        escaped += "&gt;";
        break;
      case '"':
        escaped += "&quot;";
        break;
      case '\'':
        escaped += "&apos;";
        break;
      default:
        escaped.push_back(ch);
        break;
    }
  }
  return escaped;
}

std::optional<std::string> field(std::map<std::string, std::string> const& frontmatter, std::string_view name)
{
  auto const it = frontmatter.find(std::string(name));
  if (it == frontmatter.end())
    return std::nullopt;
  return it->second;
}

ParsedMarkdown parse_markdown(std::string_view content)
{
  ParsedMarkdown parsed;
  if (!(content.starts_with("---\n") || content.starts_with("---\r\n")))
  {
    parsed.body = std::string(content);
    return parsed;
  }

  auto const body_start = content.starts_with("---\r\n") ? 5 : 4;
  auto const delimiter = content.find("\n---", body_start);
  if (delimiter == std::string_view::npos)
  {
    parsed.body = std::string(content);
    return parsed;
  }

  auto const frontmatter = content.substr(body_start, delimiter - body_start);
  std::size_t line_start = 0;
  while (line_start <= frontmatter.size())
  {
    auto const line_end = frontmatter.find('\n', line_start);
    auto line = frontmatter.substr(line_start, line_end == std::string_view::npos ? std::string_view::npos : line_end - line_start);
    if (!line.empty() && line.back() == '\r')
      line.remove_suffix(1);
    if (auto const colon = line.find(':'); colon != std::string_view::npos)
    {
      auto key = trim(line.substr(0, colon));
      auto value = strip_matching_quotes(trim(line.substr(colon + 1)));
      if (!key.empty())
        parsed.frontmatter[std::move(key)] = std::move(value);
    }
    if (line_end == std::string_view::npos)
      break;
    line_start = line_end + 1;
  }

  auto after = delimiter + 4;
  if (after < content.size() && content[after] == '\r')
    ++after;
  if (after < content.size() && content[after] == '\n')
    ++after;
  parsed.body = std::string(content.substr(after));
  return parsed;
}

ava::core::Result<std::string> read_bounded_file(std::filesystem::path const& path, std::size_t max_file_bytes)
{
  std::error_code status_error;
  auto const status = std::filesystem::symlink_status(path, status_error);
  if (status_error || std::filesystem::is_symlink(status) || !std::filesystem::is_regular_file(status))
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "subagent file is not a regular file");
    error.with_context("path", path.string());
    if (status_error)
      error.with_context("cause", status_error.message());
    return std::unexpected(std::move(error));
  }

  std::error_code size_error;
  auto const size = std::filesystem::file_size(path, size_error);
  if (size_error || size > max_file_bytes)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "subagent file is too large");
    error.with_context("path", path.string());
    error.with_context("max_bytes", std::to_string(max_file_bytes));
    if (size_error)
      error.with_context("cause", size_error.message());
    return std::unexpected(std::move(error));
  }

  std::ifstream file(path, std::ios::binary);
  if (!file)
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to open subagent file").with_context("path", path.string()));
  }
  std::string content;
  content.resize(static_cast<std::size_t>(size));
  if (!content.empty())
    file.read(content.data(), static_cast<std::streamsize>(content.size()));
  if (!file && !file.eof())
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to read subagent file").with_context("path", path.string()));
  }
  return content;
}

SubagentToolPreset parse_tool_preset(std::string value)
{
  value = trim(value);
  std::ranges::transform(value, value.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  if (value == "read-only" || value == "read_only" || value == "readonly" || value == "explore")
    return SubagentToolPreset::ReadOnly;
  return SubagentToolPreset::Inherit;
}

bool hidden_field(std::map<std::string, std::string> const& frontmatter)
{
  auto value = field(frontmatter, "hidden");
  if (!value)
    return false;
  auto text = trim(*value);
  std::ranges::transform(text, text.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return text == "true" || text == "yes" || text == "1";
}

void add_or_replace_subagent(std::vector<SubagentDefinition>& subagents, std::vector<SubagentDiagnostic>& diagnostics, SubagentDefinition subagent)
{
  auto const existing = std::ranges::find_if(subagents, [&](SubagentDefinition const& item) { return item.name == subagent.name; });
  if (existing == subagents.end())
  {
    if (subagents.size() < kMaxSubagents)
      subagents.push_back(std::move(subagent));
    else
      diagnostics.push_back(SubagentDiagnostic{.path = subagent.path, .message = "too many subagents; entry ignored"});
    return;
  }
  if (existing->builtin)
  {
    diagnostics.push_back(SubagentDiagnostic{.path = subagent.path, .message = "subagent name collides with a builtin: " + subagent.name});
    return;
  }
  *existing = std::move(subagent);
}

void load_subagent_file(std::vector<SubagentDefinition>& subagents, std::vector<SubagentDiagnostic>& diagnostics, std::filesystem::path const& path,
                        std::size_t max_file_bytes)
{
  auto content = read_bounded_file(path, max_file_bytes);
  if (!content)
  {
    diagnostics.push_back(SubagentDiagnostic{.path = path, .message = content.error().format()});
    return;
  }
  auto parsed = parse_markdown(*content);
  auto mode = field(parsed.frontmatter, "mode").value_or("subagent");
  mode = trim(mode);
  std::ranges::transform(mode, mode.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  if (mode == "primary")
    return;
  if (mode != "subagent" && mode != "all")
  {
    diagnostics.push_back(SubagentDiagnostic{.path = path, .message = "subagent mode is invalid: " + mode});
    return;
  }

  auto name = field(parsed.frontmatter, "name").value_or(path.stem().string());
  auto description = field(parsed.frontmatter, "description").value_or("");
  name = trim(name);
  description = trim(description);
  if (!valid_subagent_name(name))
  {
    diagnostics.push_back(SubagentDiagnostic{.path = path, .message = "subagent name is invalid: " + name});
    return;
  }
  if (description.empty() || description.size() > kMaxSubagentDescriptionBytes || has_control_byte(description))
  {
    diagnostics.push_back(SubagentDiagnostic{.path = path, .message = "subagent description is missing or invalid"});
    return;
  }
  if (parsed.body.size() > kMaxSubagentPromptBytes || has_control_byte(parsed.body))
  {
    diagnostics.push_back(SubagentDiagnostic{.path = path, .message = "subagent prompt is too large or invalid"});
    return;
  }
  auto tools = field(parsed.frontmatter, "tools").value_or("");
  add_or_replace_subagent(subagents, diagnostics,
                          SubagentDefinition{.name = std::move(name),
                                             .description = std::move(description),
                                             .system_prompt = std::move(parsed.body),
                                             .tool_preset = parse_tool_preset(std::move(tools)),
                                             .hidden = hidden_field(parsed.frontmatter),
                                             .builtin = false,
                                             .path = path});
}

void discover_from_root(std::vector<SubagentDefinition>& subagents, std::vector<SubagentDiagnostic>& diagnostics, std::filesystem::path const& root,
                        std::size_t max_file_bytes)
{
  if (root.empty())
    return;
  std::error_code exists_error;
  if (!std::filesystem::exists(root, exists_error))
    return;
  if (exists_error)
  {
    diagnostics.push_back(SubagentDiagnostic{.path = root, .message = "failed to inspect subagent directory"});
    return;
  }
  std::error_code iter_error;
  for (std::filesystem::recursive_directory_iterator it(root, std::filesystem::directory_options::skip_permission_denied, iter_error), end;
       !iter_error && it != end; it.increment(iter_error))
  {
    std::error_code entry_error;
    if (it->is_symlink(entry_error) || entry_error)
      continue;
    if (!it->is_regular_file(entry_error) || entry_error)
      continue;
    if (it->path().extension() != ".md")
      continue;
    load_subagent_file(subagents, diagnostics, it->path(), max_file_bytes);
  }
  if (iter_error)
    diagnostics.push_back(SubagentDiagnostic{.path = root, .message = "failed to iterate subagent directory"});
}

}  // namespace

bool valid_subagent_name(std::string_view name)
{
  if (name.empty() || name.size() > kMaxSubagentNameBytes)
    return false;
  bool last_was_separator = false;
  for (char const ch : name)
  {
    auto const byte = static_cast<unsigned char>(ch);
    bool const allowed = std::isalnum(byte) != 0 || ch == '.' || ch == '_' || ch == '-';
    if (!allowed)
      return false;
    bool const separator = ch == '.' || ch == '_' || ch == '-';
    if (separator && last_was_separator)
      return false;
    last_was_separator = separator;
  }
  return !last_was_separator;
}

std::vector<SubagentDefinition> builtin_subagents()
{
  return {
      SubagentDefinition{.name = "general",
                         .description = "General-purpose subagent for delegated coding, analysis, and verification tasks.",
                         .system_prompt = "You are AVA's general subagent. Complete the delegated task and return only the result needed by the parent agent.",
                         .tool_preset = SubagentToolPreset::Inherit,
                         .builtin = true},
      SubagentDefinition{.name = "explore",
                         .description = "Read-only subagent for fast codebase exploration and concise findings.",
                         .system_prompt = "You are AVA's explore subagent. Read, list, and search files only. Return concise findings to the parent agent.",
                         .tool_preset = SubagentToolPreset::ReadOnly,
                         .builtin = true}};
}

std::vector<std::filesystem::path> default_global_subagent_dirs()
{
  auto const paths = ava::config::xdg_paths();
  std::vector<std::filesystem::path> dirs{paths.ava_config_dir / "agents", paths.ava_config_dir / "agent"};
  if (auto home = home_dir())
  {
    dirs.push_back(*home / ".agents" / "agents");
    dirs.push_back(*home / ".agents" / "agent");
    dirs.push_back(*home / ".claude" / "agents");
    dirs.push_back(*home / ".claude" / "agent");
  }
  return dirs;
}

std::vector<std::filesystem::path> default_project_subagent_dirs(std::filesystem::path const& workspace_root)
{
  if (workspace_root.empty())
    return {};
  return {workspace_root / ".ava" / "agents",   workspace_root / ".ava" / "agent",     workspace_root / ".agents" / "agents",
          workspace_root / ".agents" / "agent", workspace_root / ".claude" / "agents", workspace_root / ".claude" / "agent"};
}

SubagentLoadResult load_subagents(SubagentLoadOptions options)
{
  if (options.global_agent_dirs.empty())
    options.global_agent_dirs = default_global_subagent_dirs();
  if (options.include_project_agents && options.project_agent_dirs.empty())
    options.project_agent_dirs = default_project_subagent_dirs(options.workspace_root);
  if (options.max_file_bytes == 0)
    options.max_file_bytes = 64 * 1024;

  SubagentLoadResult result;
  result.subagents = builtin_subagents();
  for (auto const& dir : options.global_agent_dirs) discover_from_root(result.subagents, result.diagnostics, dir, options.max_file_bytes);
  if (options.include_project_agents)
  {
    for (auto const& dir : options.project_agent_dirs) discover_from_root(result.subagents, result.diagnostics, dir, options.max_file_bytes);
  }
  std::ranges::sort(result.subagents, [](SubagentDefinition const& left, SubagentDefinition const& right) { return left.name < right.name; });
  return result;
}

SubagentDefinition const* find_subagent(std::vector<SubagentDefinition> const& subagents, std::string_view name)
{
  auto const it = std::ranges::find_if(subagents, [name](SubagentDefinition const& subagent) { return subagent.name == name; });
  return it == subagents.end() ? nullptr : &*it;
}

std::string subagent_names_csv(std::vector<SubagentDefinition> const& subagents)
{
  std::string names;
  for (auto const& subagent : subagents)
  {
    if (subagent.hidden)
      continue;
    if (!names.empty())
      names += ", ";
    names += subagent.name;
  }
  return names.empty() ? "none" : names;
}

std::string format_available_subagents_for_prompt(std::vector<SubagentDefinition> const& subagents)
{
  std::string output;
  for (auto const& subagent : subagents)
  {
    if (subagent.hidden)
      continue;
    if (output.empty())
    {
      output =
          "\n\n# Available Subagents\n"
          "Use the task tool to delegate self-contained work to a subagent when parallel exploration or isolation helps.\n"
          "<available_subagents>\n";
    }
    output += "  <subagent>\n";
    output += "    <name>" + xml_escape(subagent.name) + "</name>\n";
    output += "    <description>" + xml_escape(subagent.description) + "</description>\n";
    output += "    <tools>" + std::string(subagent.tool_preset == SubagentToolPreset::ReadOnly ? "read-only" : "inherited") + "</tools>\n";
    output += "  </subagent>\n";
  }
  if (!output.empty())
    output += "</available_subagents>\n";
  return output;
}

}  // namespace ava::agent
