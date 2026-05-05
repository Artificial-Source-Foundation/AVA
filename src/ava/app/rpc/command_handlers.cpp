#include "ava/app/rpc/command_handlers.h"

#include <atomic>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ava/app/commands.h"
#include "ava/app/events.h"
#include "ava/app/rpc/handlers.h"
#include "ava/app/rpc/serialization.h"

namespace ava::app::rpc {
namespace {

ava::core::Result<std::string> bridge_slash_command(RpcCommand const& command)
{
  if (command.type == "context") return std::string("/context");
  if (command.type == "export") return std::string("/export");
  if (command.type == "compact") {
    std::string slash_command = "/compact";
    if (command.instructions) slash_command += " " + *command.instructions;
    return slash_command;
  }
  if (is_plugin_rpc_command(command.type)) return plugin_rpc_slash_command(command);
  if (is_mcp_rpc_command(command.type)) return mcp_rpc_slash_command(command);
  return std::unexpected(invalid_rpc("unsupported command bridge RPC command"));
}

bool try_start_compact_run(RpcRunState& run_state, RpcCommand const& command, bool& compact_active_run,
                           ava::core::Error& error)
{
  std::lock_guard lock(run_state.mutex);
  if (run_state.active_run) {
    error = active_run_reject_error(command.type);
    return false;
  }
  if (command.type == "compact") {
    run_state.active_run = true;
    run_state.active_request_id = command.id;
    run_state.cancel_requested.store(false, std::memory_order_relaxed);
    compact_active_run = true;
  }
  return true;
}

}  // namespace

bool is_command_bridge_command(std::string_view type)
{
  return type == "context" || type == "export" || type == "compact" || is_plugin_rpc_command(type) ||
         is_mcp_rpc_command(type);
}

ava::core::VoidResult handle_command_bridge_command(RpcOutput& output, RuntimeSession& session,
                                                    std::mutex& session_mutex, RpcRunState& run_state,
                                                    RpcCommand const& command, RuntimeRunOptions const& runtime_options,
                                                    ava::provider::Provider const& provider,
                                                    ava::provider::Transport& transport,
                                                    ava::provider::Transport& auth_transport,
                                                    std::string_view injected_provider_id)
{
  bool compact_active_run = false;
  auto clear_compact_active_run = [&] {
    if (compact_active_run) {
      set_active_run(run_state, false);
      compact_active_run = false;
    }
  };
  auto write_command_error = [&](ava::core::Error error) -> ava::core::VoidResult {
    clear_compact_active_run();
    return write_error(output, command.id, std::move(error));
  };
  auto write_command_success = [&](std::string json) -> ava::core::VoidResult {
    clear_compact_active_run();
    return write_success(output, command.id, std::move(json));
  };

  ava::core::Error active_error = active_run_reject_error(command.type);
  if (!try_start_compact_run(run_state, command, compact_active_run, active_error)) {
    return write_error(output, command.id, active_error);
  }

  auto slash_command = bridge_slash_command(command);
  if (!slash_command) return write_command_error(slash_command.error());

  std::optional<RuntimeRunOptions> compact_runtime_options;
  std::optional<ProviderHandle> compact_provider;
  if (command.type == "compact") {
    ava::config::XdgPaths paths;
    std::string provider_id;
    {
      std::lock_guard lock(session_mutex);
      paths = session.paths;
      provider_id = session.model.provider_id;
      auto selected_provider = provider_for_session_model(session, injected_provider_id, provider);
      if (!selected_provider) return write_command_error(selected_provider.error());
      compact_provider = std::move(*selected_provider);
    }
    auto ensured = ensure_prompt_runtime_options(paths, provider_id, runtime_options, auth_transport, "compact");
    if (!ensured) return write_command_error(ensured.error());
    compact_runtime_options = std::move(*ensured);
  }

  EventBus event_bus;
  subscribe_event_envelope_writer(event_bus, output);
  std::unique_lock lock(session_mutex, std::defer_lock);
  if (command.type != "compact") lock.lock();
  auto summary_generator =
      command.type == "compact"
          ? CompactionSummaryGenerator([&](std::vector<ava::session::SessionEntry> const& entries,
                                           ava::session::CompactionConfig const& config, std::string_view instructions,
                                           std::size_t estimated_tokens) {
              return generate_compaction_summary(session, entries, config, instructions, estimated_tokens,
                                                 compact_provider->get(), transport, *compact_runtime_options);
            })
          : CompactionSummaryGenerator{};
  auto result = run_command(
      session, CommandRequest{.command = std::move(*slash_command),
                              .event_sink = make_runtime_event_bus_adapter(event_bus, rpc_event_context(command.id)),
                              .permission_resolver = runtime_options.permission_resolver,
                              .compaction_summary_generator = std::move(summary_generator),
                              .session_mutex = &session_mutex,
                              .propagate_compaction_errors = command.type == "compact"});
  if (!result) return write_command_error(result.error());
  return write_command_success(command_result_json(*result));
}

}  // namespace ava::app::rpc
