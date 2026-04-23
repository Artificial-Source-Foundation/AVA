#include "ava/tools/path_guard.hpp"

#include <sstream>
#include <stdexcept>

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

[[nodiscard]] std::filesystem::path canonical_existing_or_lexical(
    const std::filesystem::path& path,
    std::error_code& ec
) {
  if(std::filesystem::exists(path, ec) && !ec) {
    auto resolved = std::filesystem::canonical(path, ec);
    if(!ec) {
      return resolved.lexically_normal();
    }
  }

  ec.clear();
  auto normalized = std::filesystem::weakly_canonical(path, ec);
  if(ec) {
    normalized = std::filesystem::absolute(path);
  }
  return normalized.lexically_normal();
}

}  // namespace

std::filesystem::path normalize_workspace_root(const std::filesystem::path& workspace_root) {
  std::error_code ec;
  auto normalized = std::filesystem::weakly_canonical(workspace_root, ec);
  if(ec) {
    normalized = std::filesystem::absolute(workspace_root);
  }
  return normalized.lexically_normal();
}

std::filesystem::path enforce_workspace_path(
    const std::filesystem::path& workspace_root,
    const std::string& raw_path,
    const std::string& tool_name
) {
  const auto root = normalize_workspace_root(workspace_root);
  const auto input_path = std::filesystem::path(raw_path);
  const auto joined = input_path.is_absolute() ? input_path : (root / input_path);

  std::error_code ec;
  auto normalized = canonical_existing_or_lexical(joined, ec);

  if(!is_within(root, normalized)) {
    std::ostringstream oss;
    oss << "Tool '" << tool_name << "' cannot access path outside workspace: " << raw_path;
    throw std::runtime_error(oss.str());
  }

  return normalized;
}

}  // namespace ava::tools
