#include "ava/config/xdg_paths.h"
#include "ava/context/skill_loader.h"
#include "ava/core/error.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <string_view>

namespace ava::context {
namespace {

constexpr std::size_t kMaxSkillNameBytes = 64;
constexpr std::size_t kMaxSkillDescriptionBytes = 1024;

std::filesystem::path normalized_absolute(std::filesystem::path const& path)
{
  std::error_code error;
  auto normalized = std::filesystem::weakly_canonical(path, error);
  if (!error)
    return normalized.lexically_normal();
  return std::filesystem::absolute(path, error).lexically_normal();
}

std::optional<std::filesystem::path> home_dir()
{
  char const* home = std::getenv("HOME");
  if (home == nullptr || std::string_view(home).empty())
    return std::nullopt;
  auto path = std::filesystem::path(home).lexically_normal();
  if (!path.is_absolute())
    return std::nullopt;
  return path;
}

std::string trim(std::string_view value)
{
  std::size_t start = 0;
  while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])) != 0) ++start;
  auto end = value.size();
  while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) --end;
  return std::string(value.substr(start, end - start));
}

std::string strip_matching_quotes(std::string value)
{
  if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"') || (value.front() == '\'' && value.back() == '\'')))
  {
    return value.substr(1, value.size() - 2);
  }
  return value;
}

bool has_control_byte(std::string_view value)
{
  for (char const ch : value)
  {
    auto const byte = static_cast<unsigned char>(ch);
    if (byte < 0x20 || byte == 0x7F)
      return true;
  }
  return false;
}

bool valid_skill_name(std::string_view name)
{
  if (name.empty() || name.size() > kMaxSkillNameBytes)
    return false;
  bool previous_hyphen = false;
  for (char const ch : name)
  {
    auto const byte = static_cast<unsigned char>(ch);
    bool const allowed = std::islower(byte) != 0 || std::isdigit(byte) != 0 || ch == '-';
    if (!allowed)
      return false;
    if (ch == '-' && previous_hyphen)
      return false;
    previous_hyphen = ch == '-';
  }
  return name.front() != '-' && name.back() != '-';
}

std::string xml_escape(std::string_view value)
{
  std::string out;
  out.reserve(value.size());
  for (char const ch : value)
  {
    switch (ch)
    {
      case '&':
        out += "&amp;";
        break;
      case '<':
        out += "&lt;";
        break;
      case '>':
        out += "&gt;";
        break;
      case '"':
        out += "&quot;";
        break;
      case '\'':
        out += "&apos;";
        break;
      default:
        out.push_back(ch);
        break;
    }
  }
  return out;
}

ava::core::Result<std::string> read_skill_file(std::filesystem::path const& path, std::size_t max_file_bytes)
{
  std::error_code status_error;
  auto const status = std::filesystem::symlink_status(path, status_error);
  if (status_error || std::filesystem::is_symlink(status) || !std::filesystem::is_regular_file(status))
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "skill file is not a regular file");
    error.with_context("path", path.string());
    if (status_error)
      error.with_context("cause", status_error.message());
    return std::unexpected(std::move(error));
  }

  std::error_code size_error;
  auto const size = std::filesystem::file_size(path, size_error);
  if (size_error || size > max_file_bytes)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "skill file is too large");
    error.with_context("path", path.string());
    error.with_context("max_bytes", std::to_string(max_file_bytes));
    if (size_error)
      error.with_context("cause", size_error.message());
    return std::unexpected(std::move(error));
  }

  std::ifstream file(path, std::ios::binary);
  if (!file)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to open skill file");
    error.with_context("path", path.string());
    return std::unexpected(std::move(error));
  }

  std::string content;
  std::array<char, 4096> buffer{};
  while (file)
  {
    file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    if (file.gcount() > 0)
      content.append(buffer.data(), static_cast<std::size_t>(file.gcount()));
    if (content.size() > max_file_bytes)
    {
      auto error = ava::core::Error(ava::core::ErrorCategory::Io, "skill file is too large");
      error.with_context("path", path.string());
      error.with_context("max_bytes", std::to_string(max_file_bytes));
      return std::unexpected(std::move(error));
    }
  }
  if (!file.eof() && file.fail())
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed while reading skill file");
    error.with_context("path", path.string());
    return std::unexpected(std::move(error));
  }
  return content;
}

struct ParsedSkillMarkdown
{
  std::map<std::string, std::string> frontmatter;
  std::string body;
};

ParsedSkillMarkdown parse_skill_markdown(std::string_view content)
{
  ParsedSkillMarkdown parsed;
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

std::optional<std::string> field(std::map<std::string, std::string> const& frontmatter, std::string_view name)
{
  auto const it = frontmatter.find(std::string(name));
  if (it == frontmatter.end())
    return std::nullopt;
  return it->second;
}

void add_or_replace_skill(std::vector<LoadedSkill>& skills, LoadedSkill skill)
{
  auto const existing = std::ranges::find_if(skills, [&](LoadedSkill const& item) { return item.name == skill.name; });
  if (existing != skills.end())
  {
    *existing = std::move(skill);
    return;
  }
  skills.push_back(std::move(skill));
}

void load_skill_file(std::vector<LoadedSkill>& skills, std::vector<SkillDiagnostic>& diagnostics, std::filesystem::path const& skill_file,
                     SkillSourceType source_type, std::size_t max_file_bytes)
{
  auto content = read_skill_file(skill_file, max_file_bytes);
  if (!content)
  {
    diagnostics.push_back(SkillDiagnostic{.path = skill_file, .message = content.error().format()});
    return;
  }

  auto parsed = parse_skill_markdown(*content);
  auto const directory = normalized_absolute(skill_file.parent_path());
  auto name = field(parsed.frontmatter, "name").value_or(directory.filename().string());
  auto description = field(parsed.frontmatter, "description").value_or("");
  name = trim(name);
  description = trim(description);

  if (!valid_skill_name(name))
  {
    diagnostics.push_back(SkillDiagnostic{.path = skill_file, .message = "skill name is invalid: " + name});
    return;
  }
  if (description.empty() || description.size() > kMaxSkillDescriptionBytes || has_control_byte(description))
  {
    diagnostics.push_back(SkillDiagnostic{.path = skill_file, .message = "skill description is missing or invalid"});
    return;
  }

  add_or_replace_skill(skills, LoadedSkill{.name = std::move(name),
                                           .description = std::move(description),
                                           .path = normalized_absolute(skill_file),
                                           .directory = directory,
                                           .source_type = source_type,
                                           .byte_count = content->size(),
                                           .content = std::move(parsed.body)});
}

void discover_from_root(std::vector<LoadedSkill>& skills, std::vector<SkillDiagnostic>& diagnostics, std::filesystem::path const& root,
                        SkillSourceType source_type, std::size_t max_file_bytes)
{
  if (root.empty())
    return;
  std::error_code exists_error;
  if (!std::filesystem::exists(root, exists_error))
    return;
  if (exists_error)
  {
    diagnostics.push_back(SkillDiagnostic{.path = root, .message = "failed to inspect skill directory"});
    return;
  }

  auto const direct_skill = root / "SKILL.md";
  std::error_code direct_error;
  if (std::filesystem::is_regular_file(direct_skill, direct_error))
  {
    load_skill_file(skills, diagnostics, direct_skill, source_type, max_file_bytes);
    return;
  }

  std::error_code iter_error;
  for (std::filesystem::directory_iterator it(root, iter_error), end; !iter_error && it != end; it.increment(iter_error))
  {
    std::error_code entry_error;
    if (it->is_symlink(entry_error) || entry_error)
      continue;
    if (!it->is_directory(entry_error) || entry_error)
      continue;
    auto const skill_file = it->path() / "SKILL.md";
    std::error_code skill_error;
    if (!std::filesystem::exists(skill_file, skill_error) || skill_error)
      continue;
    load_skill_file(skills, diagnostics, skill_file, source_type, max_file_bytes);
  }
  if (iter_error)
    diagnostics.push_back(SkillDiagnostic{.path = root, .message = "failed to iterate skill directory"});
}

}  // namespace

std::string to_string(SkillSourceType source_type)
{
  switch (source_type)
  {
    case SkillSourceType::Global:
      return "global";
    case SkillSourceType::Project:
      return "project";
  }
  return "unknown";
}

std::vector<std::filesystem::path> default_global_skill_dirs()
{
  std::vector<std::filesystem::path> dirs{ava::config::xdg_paths().ava_config_dir / "skills"};
  if (auto home = home_dir())
  {
    dirs.push_back(*home / ".agents" / "skills");
    dirs.push_back(*home / ".claude" / "skills");
  }
  return dirs;
}

std::vector<std::filesystem::path> default_project_skill_dirs(std::filesystem::path const& workspace_root)
{
  if (workspace_root.empty())
    return {};
  return {workspace_root / ".ava" / "skills", workspace_root / ".agents" / "skills", workspace_root / ".claude" / "skills"};
}

SkillLoadResult load_skills(SkillLoadOptions options)
{
  if (options.global_skill_dirs.empty())
    options.global_skill_dirs = default_global_skill_dirs();
  if (options.include_project_skills && options.project_skill_dirs.empty())
    options.project_skill_dirs = default_project_skill_dirs(options.workspace_root);
  if (options.max_file_bytes == 0)
    options.max_file_bytes = 64 * 1024;

  SkillLoadResult result;
  for (auto const& dir : options.global_skill_dirs)
  {
    discover_from_root(result.skills, result.diagnostics, dir, SkillSourceType::Global, options.max_file_bytes);
  }
  if (options.include_project_skills)
  {
    for (auto const& dir : options.project_skill_dirs)
    {
      discover_from_root(result.skills, result.diagnostics, dir, SkillSourceType::Project, options.max_file_bytes);
    }
  }
  std::ranges::sort(result.skills, [](LoadedSkill const& left, LoadedSkill const& right) { return left.name < right.name; });
  return result;
}

std::string format_available_skills_for_prompt(std::vector<LoadedSkill> const& skills)
{
  if (skills.empty())
    return {};
  std::string output =
      "\n\n# Available Skills\n"
      "Skills provide specialized instructions and workflows for specific tasks. Use the skill tool to load a skill "
      "when a task matches its description.\n"
      "<available_skills>\n";
  for (auto const& skill : skills)
  {
    output += "  <skill>\n";
    output += "    <name>" + xml_escape(skill.name) + "</name>\n";
    output += "    <description>" + xml_escape(skill.description) + "</description>\n";
    output += "    <location>" + xml_escape(skill.path.string()) + "</location>\n";
    output += "    <scope>" + to_string(skill.source_type) + "</scope>\n";
    output += "  </skill>\n";
  }
  output += "</available_skills>\n";
  return output;
}

std::vector<std::filesystem::path> sample_skill_files(std::filesystem::path const& skill_dir, std::size_t max_files)
{
  std::vector<std::filesystem::path> files;
  std::error_code exists_error;
  if (skill_dir.empty() || !std::filesystem::exists(skill_dir, exists_error) || exists_error)
    return files;

  std::error_code iter_error;
  for (std::filesystem::recursive_directory_iterator it(skill_dir, std::filesystem::directory_options::skip_permission_denied, iter_error), end;
       !iter_error && it != end && files.size() < max_files; it.increment(iter_error))
  {
    std::error_code entry_error;
    if (it->is_symlink(entry_error) || entry_error)
      continue;
    if (!it->is_regular_file(entry_error) || entry_error)
      continue;
    if (it->path().filename() == "SKILL.md")
      continue;
    files.push_back(normalized_absolute(it->path()));
  }
  std::ranges::sort(files);
  return files;
}

std::string format_loaded_skill_for_tool(LoadedSkill const& skill, std::vector<std::filesystem::path> const& sampled_files)
{
  std::string output;
  output += "<skill_content name=\"" + xml_escape(skill.name) + "\">\n";
  output += "# Skill: " + skill.name + "\n\n";
  output += skill.content;
  if (!output.ends_with('\n'))
    output += '\n';
  output += "\nBase directory for this skill: " + skill.directory.string() + "\n";
  output += "Relative paths in this skill are relative to the base directory above.\n";
  output += "<skill_files>\n";
  for (auto const& file : sampled_files) output += "  <file>" + xml_escape(file.string()) + "</file>\n";
  output += "</skill_files>\n";
  output += "</skill_content>";
  return output;
}

}  // namespace ava::context
