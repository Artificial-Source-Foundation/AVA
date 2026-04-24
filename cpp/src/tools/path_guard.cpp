#include "ava/tools/path_guard.hpp"

#include <sstream>
#include <stdexcept>
#include <vector>

namespace ava::tools {

namespace {

[[nodiscard]] bool is_within(const std::filesystem::path& root, const std::filesystem::path& target) {
  auto root_it = root.begin();
  auto target_it = target.begin();

  for(; root_it != root.end(); ++root_it, ++target_it) {
    if(target_it == target.end() || *root_it != *target_it) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] std::filesystem::path canonical_existing_prefix_or_throw(const std::filesystem::path& path) {
  const auto absolute = std::filesystem::absolute(path).lexically_normal();
  std::vector<std::filesystem::path> suffix;

  for(auto current = absolute; !current.empty(); current = current.parent_path()) {
    std::error_code exists_ec;
    if(std::filesystem::exists(current, exists_ec) && !exists_ec) {
      std::error_code canonical_ec;
      auto resolved = std::filesystem::canonical(current, canonical_ec);
      if(canonical_ec) {
        throw std::runtime_error("Failed to resolve path: " + current.string());
      }

      for(auto it = suffix.rbegin(); it != suffix.rend(); ++it) {
        resolved /= *it;
      }
      return resolved.lexically_normal();
    }

    const auto filename = current.filename();
    if(filename.empty()) {
      break;
    }
    suffix.push_back(filename);
  }

  return absolute;
}

}  // namespace

std::filesystem::path normalize_workspace_root(const std::filesystem::path& workspace_root) {
  std::error_code ec;
  if(std::filesystem::exists(workspace_root, ec) && !ec) {
    auto normalized = std::filesystem::canonical(workspace_root, ec);
    if(!ec) {
      return normalized.lexically_normal();
    }
  }
  return std::filesystem::absolute(workspace_root).lexically_normal();
}

std::filesystem::path enforce_workspace_path(
    const std::filesystem::path& workspace_root,
    const std::string& raw_path,
    const std::string& tool_name
) {
  const auto root = normalize_workspace_root(workspace_root);
  const auto input_path = std::filesystem::path(raw_path);
  const auto joined = input_path.is_absolute() ? input_path : (root / input_path);

  const auto normalized = canonical_existing_prefix_or_throw(joined);

  if(!is_within(root, normalized)) {
    std::ostringstream oss;
    oss << "Tool '" << tool_name << "' cannot access path outside workspace: " << raw_path;
    throw std::runtime_error(oss.str());
  }

  return normalized;
}

}  // namespace ava::tools
