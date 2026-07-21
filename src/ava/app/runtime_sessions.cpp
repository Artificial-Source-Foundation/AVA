#include "sys.h"
#include "ava/app/runtime/Session.h"
#include "ava/app/runtime_sessions.h"

#include <string>

namespace ava::app {
namespace {

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
