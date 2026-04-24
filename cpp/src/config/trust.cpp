#include "ava/config/trust.hpp"

#include <fstream>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <unordered_set>

#include <nlohmann/json.hpp>

#include "ava/config/paths.hpp"
#include "ava/platform/filesystem.hpp"

namespace ava::config {
namespace {

std::mutex g_trust_cache_mutex;
std::optional<std::unordered_set<std::string>> g_trust_cache;
ava::platform::LocalFileSystem g_filesystem;

[[nodiscard]] std::string normalize_path(const std::filesystem::path& path) {
  std::error_code ec;
  const auto canonical = std::filesystem::weakly_canonical(path, ec);
  if(!ec) {
    return canonical.string();
  }
  return path.lexically_normal().string();
}

[[nodiscard]] std::unordered_set<std::string> load_trusted_set_from_disk() {
  const auto trust_path = trusted_projects_path();
  if(!g_filesystem.exists(trust_path)) {
    return {};
  }

  nlohmann::json data;
  try {
    data = nlohmann::json::parse(g_filesystem.read_file(trust_path));
  } catch(const nlohmann::json::exception&) {
    return {};
  } catch(const std::runtime_error&) {
    return {};
  }

  std::unordered_set<std::string> trusted;
  const auto trusted_it = data.find("trusted");
  if(trusted_it == data.end() || !trusted_it->is_array()) {
    return trusted;
  }

  for(const auto& item : *trusted_it) {
    if(item.is_string()) {
      trusted.insert(item.get<std::string>());
    }
  }
  return trusted;
}

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

bool is_project_trusted(const std::filesystem::path& project_root) {
  const auto canonical = normalize_path(project_root);

  {
    std::scoped_lock lock(g_trust_cache_mutex);
    if(g_trust_cache.has_value()) {
      return g_trust_cache->contains(canonical);
    }
  }

  auto loaded = load_trusted_set_from_disk();
  const auto trusted = loaded.contains(canonical);

  {
    std::scoped_lock lock(g_trust_cache_mutex);
    if(!g_trust_cache.has_value()) {
      g_trust_cache = std::move(loaded);
    }
  }

  return trusted;
}

void trust_project(const std::filesystem::path& project_root) {
  const auto trust_path = trusted_projects_path();
  const auto canonical = normalize_path(project_root);

  auto trusted = load_trusted_set_from_disk();
  trusted.insert(canonical);

  if(trust_path.has_parent_path()) {
    g_filesystem.create_dir_all(trust_path.parent_path());
  }

  nlohmann::json data;
  data["trusted"] = nlohmann::json::array();
  for(const auto& path : trusted) {
    data["trusted"].push_back(path);
  }

  const auto temp_path = trust_path.string() + ".tmp";
  g_filesystem.write_file(temp_path, data.dump(2));
#if !defined(_WIN32)
  enforce_owner_only_permissions(temp_path);
#endif
  std::filesystem::rename(temp_path, trust_path);
  enforce_owner_only_permissions(trust_path);

  std::scoped_lock lock(g_trust_cache_mutex);
  g_trust_cache.reset();
}

void clear_trust_cache_for_tests() {
  std::scoped_lock lock(g_trust_cache_mutex);
  g_trust_cache.reset();
}

}  // namespace ava::config
