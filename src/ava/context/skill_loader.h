#pragma once

#include "ava/core/result.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace ava::context {

enum class SkillSourceType
{
  Global,
  Project,
};

struct LoadedSkill
{
  std::string name;
  std::string description;
  std::filesystem::path path;
  std::filesystem::path directory;
  SkillSourceType source_type = SkillSourceType::Project;
  std::size_t byte_count = 0;
  std::string content;
};

struct SkillDiagnostic
{
  std::filesystem::path path;
  std::string message;
};

struct SkillLoadOptions
{
  std::filesystem::path workspace_root;
  std::vector<std::filesystem::path> global_skill_dirs = {};
  std::vector<std::filesystem::path> project_skill_dirs = {};
  std::size_t max_file_bytes = 64 * 1024;
};

struct SkillLoadResult
{
  std::vector<LoadedSkill> skills;
  std::vector<SkillDiagnostic> diagnostics;
};

[[nodiscard]] std::string to_string(SkillSourceType source_type);
[[nodiscard]] std::vector<std::filesystem::path> default_global_skill_dirs();
[[nodiscard]] std::vector<std::filesystem::path> default_project_skill_dirs(std::filesystem::path const& workspace_root);
[[nodiscard]] SkillLoadResult load_skills(SkillLoadOptions options);
[[nodiscard]] std::string format_available_skills_for_prompt(std::vector<LoadedSkill> const& skills);
[[nodiscard]] std::string format_loaded_skill_for_tool(LoadedSkill const& skill, std::vector<std::filesystem::path> const& sampled_files);
[[nodiscard]] std::vector<std::filesystem::path> sample_skill_files(std::filesystem::path const& skill_dir, std::size_t max_files = 10);

}  // namespace ava::context
