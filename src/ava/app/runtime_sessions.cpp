#include "sys.h"
#include "ava/app/runtime/Session.h"
#include "ava/app/runtime_sessions.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace ava::app {
namespace {

constexpr std::size_t kMaxCommandAuthorityRoots = 8;

bool path_contains(std::filesystem::path const& root, std::filesystem::path const& candidate)
{
  auto const relative = candidate.lexically_relative(root);
  auto const text = relative.generic_string();
  return !relative.empty() && relative != ".." && !text.starts_with("../");
}

void append_command_authority_root(std::vector<std::filesystem::path>& roots, std::filesystem::path root)
{
  if (root.empty())
    return;
  root = root.lexically_normal();
  if (std::ranges::any_of(roots, [&root](std::filesystem::path const& existing) { return path_contains(existing, root); }))
    return;
  std::erase_if(roots, [&root](std::filesystem::path const& existing) { return path_contains(root, existing); });
  if (roots.size() < kMaxCommandAuthorityRoots)
    roots.push_back(std::move(root));
}

runtime::OpenOptions lifecycle_options(runtime::OpenOptions options, std::filesystem::path const& workspace_root, std::filesystem::path const& current_dir)
{
  options.continuity.workspace_dir = workspace_root;
  options.continuity.current_dir = current_dir;
  options.request = {};
  return options;
}

}  // namespace

ava::permissions::PermissionRuleStore permission_rule_store_for_session(runtime::Session const& session)
{
  return ava::permissions::PermissionRuleStore{.global_rules_file = session.continuity.paths.ava_config_dir / "permission-rules.json",
                                               .workspace_rules_file = session.continuity.workspace_dir / ".ava" / "permission-rules.json",
                                               .workspace_dir = session.continuity.workspace_dir,
                                               .anchor_set = session.continuity.anchor_set};
}

std::vector<std::filesystem::path> command_authority_roots_for_session(runtime::Session const& session)
{
  std::vector<std::filesystem::path> roots;
  roots.reserve(kMaxCommandAuthorityRoots);
  append_command_authority_root(roots, session.continuity.paths.ava_config_dir);
  append_command_authority_root(roots, session.continuity.paths.ava_state_dir);
  append_command_authority_root(roots, session.continuity.paths.sessions_dir);
  append_command_authority_root(roots, session.continuity.paths.auth_file);
  append_command_authority_root(roots, ava::config::legacy_ava_credentials_path());
  append_command_authority_root(roots, ava::config::legacy_compatible_auth_path());
  // Preserve the exact active store parent as a fallback for custom/test path
  // sets whose broader sessions directory is empty or disjoint. This path is
  // derived from the active store, never reconstructed from a session ID.
  append_command_authority_root(roots, session.store.session_path().parent_path());
  return roots;
}

ava::core::Result<runtime::Session> create_runtime_session_at(runtime::OpenOptions base_options, std::filesystem::path const& workspace_root,
                                                              std::filesystem::path const& current_dir)
{
  return open_runtime_session(lifecycle_options(std::move(base_options), workspace_root, current_dir));
}

ava::core::Result<runtime::Session> open_runtime_session_at(runtime::OpenOptions base_options, std::filesystem::path const& workspace_root,
                                                            std::filesystem::path const& current_dir, std::string_view requested_session_id)
{
  auto options = lifecycle_options(std::move(base_options), workspace_root, current_dir);
  options.request.requested_session_id = std::string(requested_session_id);
  return open_runtime_session(options);
}

ava::core::Result<runtime::Session> create_runtime_session_like(runtime::Session const& current, runtime::OpenOptions const& base_options)
{
  auto options = base_options;
  options.continuity = current.continuity;
  return create_runtime_session_at(std::move(options), current.continuity.workspace_dir, current.continuity.current_dir);
}

ava::core::Result<runtime::Session> open_runtime_session_like(runtime::Session const& current, runtime::OpenOptions const& base_options,
                                                              std::string_view requested_session_id)
{
  auto options = base_options;
  options.continuity = current.continuity;
  return open_runtime_session_at(std::move(options), current.continuity.workspace_dir, current.continuity.current_dir, requested_session_id);
}

}  // namespace ava::app
