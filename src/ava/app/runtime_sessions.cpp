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

}  // namespace

std::vector<std::filesystem::path> command_authority_roots_for_session(runtime::Session const& session)
{
  std::vector<std::filesystem::path> roots;
  roots.reserve(kMaxCommandAuthorityRoots);
  append_command_authority_root(roots, session.paths().ava_config_dir);
  append_command_authority_root(roots, session.paths().ava_state_dir);
  append_command_authority_root(roots, session.paths().sessions_dir);
  append_command_authority_root(roots, session.paths().auth_file);
  append_command_authority_root(roots, ava::config::legacy_ava_credentials_path());
  append_command_authority_root(roots, ava::config::legacy_compatible_auth_path());
  // Preserve the exact active store parent as a fallback for custom/test path
  // sets whose broader sessions directory is empty or disjoint. This path is
  // derived from the active store, never reconstructed from a session ID.
  append_command_authority_root(roots, session.store.session_path().parent_path());
  return roots;
}

ava::core::Result<runtime::Session> create_runtime_session_at(runtime::OpenContext context, std::filesystem::path const& workspace_root,
                                                              std::filesystem::path const& current_dir)
{
  context.workspace_dir = workspace_root;
  context.current_dir = current_dir;
  return runtime::Session::open(context);
}

ava::core::Result<runtime::Session> open_runtime_session_at(runtime::OpenContext context, std::filesystem::path const& workspace_root,
                                                            std::filesystem::path const& current_dir, runtime::SessionLifecycleRequest request)
{
  context.workspace_dir = workspace_root;
  context.current_dir = current_dir;
  return runtime::Session::open(context, request);
}

ava::core::Result<runtime::Session> create_runtime_session_like(runtime::Session const& current, runtime::OpenContext const& base_context)
{
  auto context = current.replacement_open_context(base_context);
  return create_runtime_session_at(std::move(context), current.workspace_dir(), current.current_dir());
}

ava::core::Result<runtime::Session> open_runtime_session_like(runtime::Session const& current, runtime::OpenContext const& base_context,
                                                              runtime::SessionLifecycleRequest request)
{
  auto context = current.replacement_open_context(base_context);
  return open_runtime_session_at(std::move(context), current.workspace_dir(), current.current_dir(), std::move(request));
}

}  // namespace ava::app
