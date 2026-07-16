#include "sys.h"
#include "ava/app/EventEnvelope.h"
#include "ava/app/plugin_event_hooks.h"

#include "ava/app/runtime.h"

#include "ava/plugin/diagnostics.h"
#include "ava/plugin/discovery.h"
#include "ava/plugin/enablement.h"
#include "ava/plugin/runner.h"
#include "ava/plugin/tool_broker.h"

#include "ava/core/ids.h"

#include <algorithm>
#include <cctype>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ava::app {
namespace {

struct PluginEventHookBinding {
  ava::plugin::PluginManifest manifest;
  ava::plugin::PluginEventHookContribution hook;
};

std::string normalized_event_name(std::string_view event)
{
  std::string normalized;
  normalized.reserve(event.size());
  for (char const ch : event) {
    if (ch == '.') {
      normalized.push_back('_');
      continue;
    }
    normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
  }
  return normalized;
}

bool event_matches(std::string_view hook_event, std::string_view runtime_event)
{
  return normalized_event_name(hook_event) == normalized_event_name(runtime_event);
}

bool contains(std::vector<std::string> const& values, std::string_view value)
{
  return std::ranges::find(values, value) != values.end();
}

std::string observation_key(ava::plugin::PluginManifest const& manifest, std::string_view event)
{
  return manifest.id + "\n" + std::string(event);
}

class PluginEventObserverState final {
 public:
  explicit PluginEventObserverState(PluginEventObserverOptions options) : options_(std::move(options)) {}

  void observe(runtime::RuntimeEvent const& event)
  {
    ensure_loaded();
    if (hooks_.empty()) return;

    auto const event_name = to_string(event.type);
    auto const payload = to_event_envelope(event).payload_json;
    for (auto const& binding : hooks_) {
      if (!event_matches(binding.hook.event, event_name)) continue;
      if (!ensure_launch_permission(binding.manifest)) continue;
      if (!ensure_observe_permission(binding.manifest, binding.hook.event)) continue;

      ava::plugin::PluginRunnerOptions runner_options;
      runner_options.workspace_dir = options_.workspace_dir;
      auto process = ava::plugin::PluginProcess::start(binding.manifest, runner_options);
      if (!process) {
        notify_failure(binding, process.error());
        continue;
      }
      auto const hook_label = binding.manifest.id + ":" + binding.hook.event;
      auto proxy_handler = ava::plugin::make_core_service_proxy_handler(
          permission_context("plugin_event_observe"), binding.manifest, "event_hook", binding.hook.event,
          hook_label, event.call_id);
      auto observed = (*process)->observe_event(event_name, payload, event.call_id, options_.cancel_requested,
                                                 std::move(proxy_handler));
      if (!observed) notify_failure(binding, observed.error());
      auto shutdown = (*process)->shutdown();
      if (!shutdown) notify_failure(binding, shutdown.error());
    }
  }

 private:
  void notify_failure(PluginEventHookBinding const& binding, ava::core::Error const& error) const
  {
    if (!options_.hook_failure_sink) return;
    try {
      options_.hook_failure_sink(binding.manifest.id, binding.hook.event, error);
    } catch (...) {
    }
  }

  void ensure_loaded()
  {
    if (loaded_) return;
    loaded_ = true;
    if (options_.workspace_dir.empty()) return;

    auto discovery_options = ava::plugin::default_plugin_discovery_options(options_.workspace_dir);
    if (!options_.plugin_global_plugins_dir.empty()) {
      discovery_options.global_plugins_dir = options_.plugin_global_plugins_dir;
    }
    if (!options_.include_project_plugins) {
      discovery_options.project_plugins_dir.clear();
    } else if (!options_.plugin_project_plugins_dir.empty()) {
      discovery_options.project_plugins_dir = options_.plugin_project_plugins_dir;
    }
    auto enablement_file = options_.plugin_enablement_file;
    if (enablement_file.empty()) enablement_file = ava::plugin::default_plugin_enablement_file();

    auto diagnostics =
        ava::plugin::collect_plugin_diagnostics(discovery_options, enablement_file, options_.workspace_dir);
    for (auto const& status : diagnostics.plugins) {
      if (!status.enabled) continue;
      for (auto const& hook : status.plugin.manifest.contributes.event_hooks) {
        hooks_.push_back(PluginEventHookBinding{.manifest = status.plugin.manifest, .hook = hook});
      }
    }
  }

  ava::tools::ToolContext permission_context(std::string_view tool_name) const
  {
    return ava::tools::ToolContext{.workspace_dir = options_.workspace_dir,
                                    .mode = options_.mode,
                                    .permission_resolver = options_.permission_resolver,
                                     .permission_audit_sink = options_.permission_audit_sink,
                                     .cancel_requested = options_.cancel_requested,
                                     .permission_tool_name = std::string(tool_name),
                                     .current_tool_name = std::string(tool_name),
                                     .session_id = options_.session_id,
                                     .provider_id = options_.provider_id,
                                     .model_id = options_.model_id,
                                     .current_dir = options_.current_dir};
  }

  bool ensure_launch_permission(ava::plugin::PluginManifest const& manifest)
  {
    if (contains(launch_allowed_plugins_, manifest.id)) return true;
    if (contains(launch_denied_plugins_, manifest.id)) return false;

    auto const context = permission_context("plugin_event_observe");
    auto permission =
        ava::tools::ensure_permission(context, ava::permissions::Operation::PluginExecute, manifest.path, manifest.id,
                                      "plugin_event_observe", "plugin event hook launch requires permission");
    if (permission) {
      launch_allowed_plugins_.push_back(manifest.id);
      return true;
    }
    launch_denied_plugins_.push_back(manifest.id);
    return false;
  }

  bool ensure_observe_permission(ava::plugin::PluginManifest const& manifest, std::string_view event)
  {
    auto const key = observation_key(manifest, event);
    if (contains(observe_allowed_, key)) return true;
    if (contains(observe_denied_, key)) return false;

    auto const context = permission_context("plugin_event_observe");
    auto const command = manifest.id + ":" + std::string(event);
    auto permission =
        ava::tools::ensure_permission(context, ava::permissions::Operation::PluginEventObserve, manifest.path, command,
                                      "plugin_event_observe", "plugin event observation requires permission");
    if (permission) {
      observe_allowed_.push_back(key);
      return true;
    }
    observe_denied_.push_back(key);
    return false;
  }

  PluginEventObserverOptions options_;
  bool loaded_ = false;
  std::vector<PluginEventHookBinding> hooks_;
  std::vector<std::string> launch_allowed_plugins_;
  std::vector<std::string> launch_denied_plugins_;
  std::vector<std::string> observe_allowed_;
  std::vector<std::string> observe_denied_;
};

ava::core::VoidResult append_permission_decision(ava::session::SessionStore& store,
                                                 ava::tools::PermissionAuditEvent const& event)
{
  return store.append(ava::session::SessionEntry{.id = ava::core::make_id("entry"),
                                                 .parent_id = "",
                                                 .type = ava::session::EntryType::PermissionDecision,
                                                 .timestamp = ava::session::now_timestamp(),
                                                 .data_json = ava::tools::permission_audit_data_json(event)});
}

}  // namespace

PluginEventObserverOptions plugin_event_observer_options(runtime::RuntimeSession& session,
                                                         ava::permissions::PermissionResolver permission_resolver,
                                                         std::mutex* session_mutex)
{
  return PluginEventObserverOptions{
      .workspace_dir = session.workspace_dir,
      .plugin_global_plugins_dir = session.paths.ava_config_dir / "plugins",
      .plugin_project_plugins_dir = project_resources_trusted(session.project_trust)
                                        ? session.workspace_dir / ".ava" / "plugins"
                                        : std::filesystem::path{},
      .plugin_enablement_file = session.paths.ava_state_dir / "plugin-enablement.json",
      .include_project_plugins = project_resources_trusted(session.project_trust),
      .mode = session.mode,
      .permission_resolver = std::move(permission_resolver),
      .permission_audit_sink = [&store = session.store,
                                  session_mutex](ava::tools::PermissionAuditEvent const& event) -> ava::core::VoidResult {
        if (session_mutex) {
          std::lock_guard lock(*session_mutex);
          return append_permission_decision(store, event);
        }
        return append_permission_decision(store, event);
      },
      .cancel_requested = nullptr,
      .session_id = session.store.session_id(),
      .provider_id = session.model.provider_id,
      .model_id = session.model.model_id,
      .current_dir = session.current_dir};
}

runtime::RuntimeEventSink make_plugin_event_observer_sink(PluginEventObserverOptions options, runtime::RuntimeEventSink next)
{
  auto state = std::make_shared<PluginEventObserverState>(std::move(options));
  return [state = std::move(state), next = std::move(next)](runtime::RuntimeEvent const& event) -> ava::core::VoidResult {
    state->observe(event);
    return emit_event(next, event);
  };
}

}  // namespace ava::app
