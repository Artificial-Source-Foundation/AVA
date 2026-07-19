#include "sys.h"
#include "ava/app/runtime_sessions.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace ava::app {
namespace {

constexpr std::size_t kMaxCommandAuthorityRoots = 8;

void append_command_authority_root(std::vector<std::filesystem::path>& roots, std::filesystem::path root)
{
  if (root.empty())
    return;
  root = root.lexically_normal();
  if (std::ranges::find(roots, root) != roots.end() || roots.size() == kMaxCommandAuthorityRoots)
    return;
  roots.push_back(std::move(root));
}

runtime::OpenOptions lifecycle_options(runtime::OpenOptions options, std::filesystem::path const& workspace_root, std::filesystem::path const& current_dir)
{
  options.workspace_dir = workspace_root;
  options.current_dir = current_dir;
  options.requested_session_id = std::nullopt;
  options.fork_session_id = std::nullopt;
  options.continue_last_session = false;
  options.sessionless = false;
  options.initial_reasoning_level = std::nullopt;
  return options;
}

}  // namespace

ava::permissions::PermissionRuleStore permission_rule_store_for_session(runtime::Session const& session)
{
  return ava::permissions::PermissionRuleStore{.global_rules_file = session.paths.ava_config_dir / "permission-rules.json",
                                              .workspace_rules_file = session.workspace_dir / ".ava" / "permission-rules.json",
                                              .workspace_dir = session.workspace_dir};
}

std::vector<std::filesystem::path> command_authority_roots_for_session(runtime::Session const& session)
{
  std::vector<std::filesystem::path> roots;
  roots.reserve(2);
  append_command_authority_root(roots, session.paths.ava_config_dir);
  // This is derived from the active exact SessionStore, never reconstructed
  // from a session ID or reacquired by pathname.
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
  options.requested_session_id = std::string(requested_session_id);
  return open_runtime_session(options);
}

ava::core::Result<runtime::Session> create_runtime_session_like(runtime::Session const& current, runtime::OpenOptions const& base_options)
{
  auto options = base_options;
  options.mode = current.mode;
  options.paths = current.paths;
  return create_runtime_session_at(std::move(options), current.workspace_dir, current.current_dir);
}

ava::core::Result<runtime::Session> open_runtime_session_like(runtime::Session const& current, runtime::OpenOptions const& base_options,
                                                            std::string_view requested_session_id)
{
  auto options = base_options;
  options.mode = current.mode;
  options.paths = current.paths;
  return open_runtime_session_at(std::move(options), current.workspace_dir, current.current_dir, requested_session_id);
}

}  // namespace ava::app
