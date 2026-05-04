#pragma once

#include <filesystem>
#include <functional>
#include <mutex>
#include <string_view>

#include "ava/agent/mode.h"
#include "ava/app/events.h"
#include "ava/core/error.h"
#include "ava/permissions/permission.h"
#include "ava/tools/file_tools.h"

namespace ava::app {

struct RuntimeSession;

using PluginEventHookFailureSink =
    std::function<void(std::string_view plugin_id, std::string_view event_name, const ava::core::Error& error)>;

struct PluginEventObserverOptions {
  std::filesystem::path workspace_dir;
  std::filesystem::path plugin_global_plugins_dir;
  std::filesystem::path plugin_project_plugins_dir;
  std::filesystem::path plugin_enablement_file;
  ava::agent::Mode mode = ava::agent::Mode::Build;
  ava::permissions::PermissionResolver permission_resolver = nullptr;
  ava::tools::PermissionAuditSink permission_audit_sink = nullptr;
  PluginEventHookFailureSink hook_failure_sink = nullptr;
};

[[nodiscard]] PluginEventObserverOptions plugin_event_observer_options(
    RuntimeSession& session, ava::permissions::PermissionResolver permission_resolver = nullptr,
    std::mutex* session_mutex = nullptr);

// Event hooks are observational and best-effort: hook launch, protocol, timeout,
// and shutdown failures are intentionally not surfaced to the originating event.
[[nodiscard]] RuntimeEventSink make_plugin_event_observer_sink(PluginEventObserverOptions options,
                                                               RuntimeEventSink next = nullptr);

}  // namespace ava::app
