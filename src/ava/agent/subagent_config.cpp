#include "sys.h"
#include "ava/agent/subagent_config.h"
#include "ava/app/runtime/command_names.h"
#include "ava/config/xdg_paths.h"
#include "ava/core/json.h"
#include "ava/core/string_utils.h"
#include "ava/app/runtime/markdown_files.h"

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

std::optional<std::filesystem::path> home_dir()
{
  if (auto const* home = std::getenv("HOME"); home != nullptr && *home != '\0')
    return std::filesystem::path(home);
  return std::nullopt;
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

SubagentToolPreset parse_tool_preset(std::string value)
{
  value = core::trim(value);
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
  auto text = core::trim(*value);
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
  auto content = app::runtime::read_bounded_file(path, max_file_bytes);
  if (!content)
  {
    diagnostics.push_back(SubagentDiagnostic{.path = path, .message = content.error().format()});
    return;
  }
  auto parsed = app::runtime::parse_markdown(*content);
  auto mode = field(parsed.frontmatter, "mode").value_or("subagent");
  mode = core::trim(mode);
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
  name = core::trim(name);
  description = core::trim(description);
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
