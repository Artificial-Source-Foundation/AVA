#include "sys.h"
#include "ava/config/xdg_paths.h"
#include "ava/context/markdown_resource.h"
#include "ava/context/skill_loader.h"
#include "ava/core/error.h"
#include "ava/core/path.h"
#include "ava/core/string_utils.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <map>
#include <optional>
#include <set>
#include <string_view>

namespace ava::context {
namespace {

constexpr std::size_t kMaxSkillNameBytes = 64;
constexpr std::size_t kMaxSkillDescriptionBytes = 1024;
constexpr std::size_t kMaxDeclaredSkillNameBytes = 96;

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

bool valid_declared_skill_name(std::string_view name)
{
  if (name.empty() || name.size() > kMaxDeclaredSkillNameBytes)
    return false;
  for (char const ch : name)
  {
    auto const byte = static_cast<unsigned char>(ch);
    bool const allowed = std::isalnum(byte) != 0 || ch == '_' || ch == '-' || ch == '.';
    if (!allowed)
      return false;
  }
  return true;
}

bool valid_skill_name_for_source(std::string_view name, SkillSourceType source_type)
{
  if (source_type == SkillSourceType::Plugin)
    return valid_declared_skill_name(name);
  return valid_skill_name(name);
}

bool valid_skill_description_for_source(std::string_view description, SkillSourceType source_type)
{
  if (description.size() > kMaxSkillDescriptionBytes || has_control_byte(description))
    return false;
  return source_type == SkillSourceType::Plugin || !description.empty();
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
  auto content = read_resource_file(skill_file, {.max_bytes = max_file_bytes, .resource_description = "skill file"});
  if (!content)
  {
    diagnostics.push_back(SkillDiagnostic{.path = skill_file, .message = content.error().format()});
    return;
  }

  auto parsed = parse_markdown(*content);
  auto const directory = ava::core::normalized_absolute_path(skill_file.parent_path());
  auto name = field(parsed.frontmatter, "name").value_or(directory.filename().string());
  auto description = field(parsed.frontmatter, "description").value_or("");
  name = core::trim(name);
  description = core::trim(description);

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
                                           .path = ava::core::normalized_absolute_path(skill_file),
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
    case SkillSourceType::Plugin:
      return "plugin";
  }
  return "unknown";
}

std::vector<std::filesystem::path> default_global_skill_dirs(ava::config::XdgPaths const& paths)
{
  std::vector<std::filesystem::path> dirs{paths.ava_config_dir / "skills"};
  if (auto home = home_dir())
  {
    dirs.push_back(*home / ".agents" / "skills");
    dirs.push_back(*home / ".claude" / "skills");
  }
  return dirs;
}

std::vector<std::filesystem::path> default_global_skill_dirs()
{
  return default_global_skill_dirs(ava::config::xdg_paths());
}

std::vector<std::filesystem::path> default_project_skill_dirs(std::filesystem::path const& workspace_root)
{
  if (workspace_root.empty())
    return {};
  return {workspace_root / ".ava" / "skills", workspace_root / ".agents" / "skills", workspace_root / ".claude" / "skills"};
}

ava::core::Result<LoadedSkill> load_declared_skill_content(DeclaredSkillFileOptions options, std::string content)
{
  if (options.max_file_bytes == 0)
    options.max_file_bytes = 64 * 1024;
  if (content.size() > options.max_file_bytes)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "skill file is too large");
    error.with_context("path", options.path.string());
    error.with_context("max_bytes", std::to_string(options.max_file_bytes));
    return std::unexpected(std::move(error));
  }

  auto const byte_count = content.size();
  auto parsed = parse_markdown(content);
  auto name = core::trim(options.name);
  auto description = core::trim(options.description);

  if (!valid_skill_name_for_source(name, options.source_type))
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "skill name is invalid");
    error.with_context("path", options.path.string());
    error.with_context("skill", name);
    return std::unexpected(std::move(error));
  }
  if (!valid_skill_description_for_source(description, options.source_type))
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "skill description is missing or invalid");
    error.with_context("path", options.path.string());
    error.with_context("skill", name);
    return std::unexpected(std::move(error));
  }

  auto const directory = ava::core::normalized_absolute_path(options.path.parent_path());
  return LoadedSkill{.name = std::move(name),
                     .description = std::move(description),
                     .path = ava::core::normalized_absolute_path(options.path),
                     .directory = directory,
                     .source_type = options.source_type,
                     .byte_count = byte_count,
                     .content = std::move(parsed.body)};
}

ava::core::Result<LoadedSkill> load_declared_skill_file(DeclaredSkillFileOptions options)
{
  if (options.max_file_bytes == 0)
    options.max_file_bytes = 64 * 1024;

  if (options.preloaded_content)
  {
    auto content = std::move(*options.preloaded_content);
    options.preloaded_content.reset();
    return load_declared_skill_content(std::move(options), std::move(content));
  }

  auto content = read_resource_file(options.path, {.max_bytes = options.max_file_bytes, .resource_description = "skill file"});
  if (!content)
    return std::unexpected(std::move(content.error()));
  return load_declared_skill_content(std::move(options), std::move(*content));
}

SkillLoadResult load_skills(SkillLoadOptions options)
{
  if (options.include_global_skills && options.global_skill_dirs.empty())
    options.global_skill_dirs = default_global_skill_dirs();
  if (options.include_project_skills && options.project_skill_dirs.empty())
    options.project_skill_dirs = default_project_skill_dirs(options.workspace_root);
  if (options.max_file_bytes == 0)
    options.max_file_bytes = 64 * 1024;

  SkillLoadResult result;
  if (options.include_global_skills)
  {
    for (auto const& dir : options.global_skill_dirs)
    {
      discover_from_root(result.skills, result.diagnostics, dir, SkillSourceType::Global, options.max_file_bytes);
    }
  }
  if (options.include_project_skills)
  {
    for (auto const& dir : options.project_skill_dirs)
    {
      discover_from_root(result.skills, result.diagnostics, dir, SkillSourceType::Project, options.max_file_bytes);
    }
  }
  for (auto declared : options.declared_skill_files)
  {
    if (declared.max_file_bytes == 0)
      declared.max_file_bytes = options.max_file_bytes;
    auto const diagnostic_path = declared.path;
    auto skill = load_declared_skill_file(std::move(declared));
    if (!skill)
    {
      result.diagnostics.push_back(SkillDiagnostic{.path = diagnostic_path, .message = skill.error().format()});
      continue;
    }
    add_or_replace_skill(result.skills, std::move(*skill));
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
    files.push_back(ava::core::normalized_absolute_path(it->path()));
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
