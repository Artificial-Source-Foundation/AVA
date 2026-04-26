#include "ava/config/project_state.hpp"

#include <algorithm>
#include <stdexcept>

#include "ava/platform/filesystem.hpp"

namespace ava::config {
namespace {

ava::platform::LocalFileSystem g_filesystem;

void enforce_owner_only_permissions(const std::filesystem::path& path) {
#if !defined(_WIN32)
  std::error_code ec;
  std::filesystem::permissions(
      path,
      std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
      std::filesystem::perm_options::replace,
      ec
  );
  if(ec) {
    throw std::runtime_error("Failed to set secure permissions on file: " + path.string() + " (" + ec.message() + ")");
  }
#else
  (void)path;
#endif
}

}  // namespace

std::filesystem::path project_state_path(const std::filesystem::path& project_root) {
  return project_root / ".ava" / "state.json";
}

ProjectState ProjectState::load(const std::filesystem::path& project_root) {
  const auto path = project_state_path(project_root);
  if(!g_filesystem.exists(path)) {
    return {};
  }

  try {
    return nlohmann::json::parse(g_filesystem.read_file(path)).get<ProjectState>();
  } catch(const nlohmann::json::exception&) {
    return {};
  } catch(const std::runtime_error&) {
    return {};
  }
}

void ProjectState::push_recent_model(std::string key) {
  recent_models.erase(std::remove(recent_models.begin(), recent_models.end(), key), recent_models.end());
  recent_models.insert(recent_models.begin(), std::move(key));
  if(recent_models.size() > 5U) {
    recent_models.resize(5U);
  }
}

void ProjectState::save(const std::filesystem::path& project_root) const {
  const auto path = project_state_path(project_root);
  if(path.has_parent_path()) {
    g_filesystem.create_dir_all(path.parent_path());
  }
  const auto temp_path = path.string() + ".tmp";
  g_filesystem.write_file(temp_path, nlohmann::json(*this).dump(2));
  enforce_owner_only_permissions(temp_path);
  std::filesystem::rename(temp_path, path);
  enforce_owner_only_permissions(path);
}

void to_json(nlohmann::json& j, const ProjectState& value) {
  j = nlohmann::json::object();
  if(value.last_provider.has_value()) {
    j["last_provider"] = *value.last_provider;
  }
  if(value.last_model.has_value()) {
    j["last_model"] = *value.last_model;
  }
  if(!value.recent_models.empty()) {
    j["recent_models"] = value.recent_models;
  }
  if(value.plan_model.has_value()) {
    j["plan_model"] = *value.plan_model;
  }
  if(value.code_model.has_value()) {
    j["code_model"] = *value.code_model;
  }
}

void from_json(const nlohmann::json& j, ProjectState& value) {
  if(j.contains("last_provider") && !j.at("last_provider").is_null()) {
    value.last_provider = j.at("last_provider").get<std::string>();
  }
  if(j.contains("last_model") && !j.at("last_model").is_null()) {
    value.last_model = j.at("last_model").get<std::string>();
  }
  if(j.contains("recent_models") && j.at("recent_models").is_array()) {
    value.recent_models = j.at("recent_models").get<std::vector<std::string>>();
  }
  if(j.contains("plan_model") && !j.at("plan_model").is_null()) {
    value.plan_model = j.at("plan_model").get<std::string>();
  }
  if(j.contains("code_model") && !j.at("code_model").is_null()) {
    value.code_model = j.at("code_model").get<std::string>();
  }
}

}  // namespace ava::config
