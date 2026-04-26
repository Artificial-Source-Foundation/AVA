#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace ava::config {

struct ProjectState {
  std::optional<std::string> last_provider;
  std::optional<std::string> last_model;
  std::vector<std::string> recent_models;
  std::optional<std::string> plan_model;
  std::optional<std::string> code_model;

  [[nodiscard]] static ProjectState load(const std::filesystem::path& project_root);
  void push_recent_model(std::string key);
  void save(const std::filesystem::path& project_root) const;
};

[[nodiscard]] std::filesystem::path project_state_path(const std::filesystem::path& project_root);

void to_json(nlohmann::json& j, const ProjectState& value);
void from_json(const nlohmann::json& j, ProjectState& value);

}  // namespace ava::config
