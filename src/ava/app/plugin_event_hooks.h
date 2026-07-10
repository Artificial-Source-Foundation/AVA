#pragma once

#include "ava/app/events.h"
#include "ava/agent/mode.h"
#include "ava/tools/file_tools.h"
#include "ava/permissions/permission.h"
#include "ava/core/error.h"

#include <filesystem>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>

namespace ava::app {

struct RuntimeSession;

using PluginEventHookFailureSink = std::function<void(std::string_view plugin_id, std::string_view event_name, ava::core::Error const& error)>;

struct PluginEventObserverOptions
{
  std::filesystem::path workspace_dir;
  std::filesystem::path plugin_global_plugins_dir;
  std::filesystem::path plugin_project_plugins_dir;
  std::filesystem::path plugin_enablement_file;
  bool include_project_plugins = true;
  ava::agent::Mode mode = ava::agent::Mode::Build;
  ava::permissions::PermissionResolver permission_resolver = nullptr;
  ava::tools::PermissionAuditSink permission_audit_sink = nullptr;
  std::function<bool()> cancel_requested = nullptr;
  PluginEventHookFailureSink hook_failure_sink = nullptr;
  std::string session_id;
  std::string provider_id;
  std::string model_id;
  std::filesystem::path current_dir;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

[[nodiscard]] PluginEventObserverOptions plugin_event_observer_options(RuntimeSession& session,
                                                                       ava::permissions::PermissionResolver permission_resolver = nullptr,
                                                                       std::mutex* session_mutex = nullptr);

// Event hooks are observational and best-effort: hook launch, protocol, timeout,
// and shutdown failures are intentionally not surfaced to the originating event.
[[nodiscard]] RuntimeEventSink make_plugin_event_observer_sink(PluginEventObserverOptions options, RuntimeEventSink next = nullptr);

}  // namespace ava::app
