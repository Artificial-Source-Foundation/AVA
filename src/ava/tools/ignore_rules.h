#pragma once

#include <filesystem>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

#include "ava/core/result.h"

namespace ava::tools {

[[nodiscard]] bool is_git_dir(std::filesystem::path const& path);
[[nodiscard]] bool is_generated_dir(std::filesystem::path const& path);

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
  [[nodiscard]] static ava::core::Result<IgnoreMatcher> load(std::filesystem::path const& workspace_dir);

  [[nodiscard]] bool ignored(std::filesystem::path const& path, bool is_directory) const;

 private:
  explicit IgnoreMatcher(std::filesystem::path workspace_dir);

  [[nodiscard]] static bool rule_matches(Rule const& rule, std::string_view relative_to_base, bool is_directory);
  [[nodiscard]] ava::core::Result<void> load_rules();
  [[nodiscard]] ava::core::Result<void> load_file(std::filesystem::path const& ignore_file);
  [[nodiscard]] std::string relative_to_workspace(std::filesystem::path const& path) const;

  std::filesystem::path workspace_dir_;
  std::vector<Rule> rules_;
};

}  // namespace ava::tools
