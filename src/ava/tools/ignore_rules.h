#pragma once

#include <filesystem>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

#include "ava/core/result.h"

namespace ava::tools {

[[nodiscard]] bool is_git_dir(const std::filesystem::path& path);
[[nodiscard]] bool is_generated_dir(const std::filesystem::path& path);

class IgnoreMatcher {
 private:
  struct Rule {
    std::string base_relative;
    std::string pattern;
    std::regex matcher;
    bool negated = false;
    bool directory_only = false;
    bool anchored = false;
    bool contains_slash = false;
  };

 public:
  [[nodiscard]] static ava::core::Result<IgnoreMatcher> load(const std::filesystem::path& workspace_dir);

  [[nodiscard]] bool ignored(const std::filesystem::path& path, bool is_directory) const;

 private:
  explicit IgnoreMatcher(std::filesystem::path workspace_dir);

  [[nodiscard]] static bool rule_matches(const Rule& rule, std::string_view relative_to_base, bool is_directory);
  [[nodiscard]] ava::core::Result<void> load_rules();
  [[nodiscard]] ava::core::Result<void> load_file(const std::filesystem::path& ignore_file);
  [[nodiscard]] std::string relative_to_workspace(const std::filesystem::path& path) const;

  std::filesystem::path workspace_dir_;
  std::vector<Rule> rules_;
};

}  // namespace ava::tools
