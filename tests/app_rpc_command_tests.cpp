#include <atomic>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "ava/agent/mode.h"
#include "ava/app/rpc/command_handlers.h"
#include "ava/app/rpc/run_state.h"
#include "ava/app/runtime.h"
#include "ava/config/prompt_config.h"
#include "ava/config/xdg_paths.h"
#include "ava/provider/openai_provider.h"
#include "tests/support/fake_transport.h"
#include "tests/support/test_harness.h"

namespace {

ava::config::XdgPaths rpc_command_test_paths(std::filesystem::path const& root)
{
  auto const config_home = root / "config";
  auto const state_home = root / "state";
  auto const data_home = root / "data";
  auto const ava_config = config_home / "ava";
  auto const ava_state = state_home / "ava";
  return ava::config::XdgPaths{.config_home = config_home,
                               .state_home = state_home,
                               .data_home = data_home,
                               .ava_config_dir = ava_config,
                               .ava_state_dir = ava_state,
                               .auth_file = ava_config / "auth.json",
                               .compaction_file = ava_config / "compaction.json",
                               .global_agents_file = ava_config / "AGENTS.md",
                               .models_file = ava_config / "models.json",
                               .prompts_dir = ava_config / "prompts",
                               .sessions_dir = ava_state / "sessions"};
}

ava::app::RuntimeSession make_rpc_command_session(std::string const& name)
{
  auto const root = temp_root() / name;
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  {
    std::ofstream context_file(workspace / "AGENTS.md", std::ios::binary | std::ios::trunc);
    context_file << "rpc command bridge context\n";
  }

  ava::app::RuntimeOpenOptions options;
  options.workspace_dir = workspace;
  options.current_dir = workspace;
  options.paths = rpc_command_test_paths(root);
  auto session = ava::app::open_runtime_session(options);
  expect(session.has_value(), "RPC command bridge test opens runtime session");
  if (!session) {
    return ava::app::RuntimeSession{
        .store = ava::session::SessionStore(ava::session::SessionStoreOptions{
            .root_dir = options.paths.sessions_dir, .workspace_dir = workspace, .session_id = "rpc-command-fallback"}),
        .mode = ava::agent::Mode::Build,
        .model = {},
        .prompt = ava::config::PromptSelection{},
        .paths = options.paths,
        .workspace_dir = workspace,
        .current_dir = workspace,
        .context_sources = {},
        .system_prompt = {},
        .reasoning = std::nullopt,
        .created = false};
  }
  return std::move(*session);
}

ava::app::RpcCommand command_bridge_command(std::string id, std::string type)
{
  ava::app::RpcCommand command;
  command.id = std::move(id);
  command.type = std::move(type);
  return command;
}

ava::core::VoidResult handle_bridge(ava::app::rpc::RpcOutput& output, ava::app::RuntimeSession& session,
                                    std::mutex& session_mutex, ava::app::rpc::RpcRunState& run_state,
                                    ava::app::RpcCommand const& command,
                                    ava::app::RuntimeRunOptions const& runtime_options,
                                    ava::provider::Transport& transport, std::string_view injected_provider_id = {})
{
  ava::provider::OpenAIProvider const provider("https://api.example.test");
  return ava::app::rpc::handle_command_bridge_command(
      output, session, session_mutex, run_state, command, runtime_options, provider, transport, transport,
      injected_provider_id.empty() ? session.model.provider_id : injected_provider_id);
}

void test_rpc_command_bridge_context_and_classification()
{
  expect(ava::app::rpc::is_command_bridge_command("context") && ava::app::rpc::is_command_bridge_command("export") &&
             ava::app::rpc::is_command_bridge_command("compact") &&
             ava::app::rpc::is_command_bridge_command("list_plugins") &&
             ava::app::rpc::is_command_bridge_command("list_mcp_servers") &&
             !ava::app::rpc::is_command_bridge_command("prompt"),
         "RPC command bridge classification covers command-backed RPC commands only");

  auto session = make_rpc_command_session("app-rpc-command-bridge-context");
  std::mutex session_mutex;
  std::ostringstream stream;
  ava::app::rpc::RpcOutput output(stream);
  ava::app::rpc::RpcRunState run_state;
  ava::app::RuntimeRunOptions runtime_options;
  ava::tests::FakeTransport transport({});

  auto context = command_bridge_command("ctx", "context");
  auto handled = handle_bridge(output, session, session_mutex, run_state, context, runtime_options, transport);
  auto const jsonl = stream.str();

  expect(handled.has_value(), "RPC command bridge handles context command");
  expect(jsonl.find("\"id\":\"ctx\"") != std::string::npos && jsonl.find("\"handled\":true") != std::string::npos &&
             jsonl.find("workspace  " + (session.workspace_dir / "AGENTS.md").string()) != std::string::npos,
         "RPC command bridge returns command result JSON for context");
}

void test_rpc_command_bridge_errors_and_compact_cleanup()
{
  auto session = make_rpc_command_session("app-rpc-command-bridge-errors");
  std::mutex session_mutex;
  std::ostringstream stream;
  ava::app::rpc::RpcOutput output(stream);
  ava::app::RuntimeRunOptions runtime_options;
  ava::tests::FakeTransport transport({});

  ava::app::rpc::RpcRunState active_run_state;
  ava::app::rpc::set_active_run(active_run_state, true, "active-prompt");
  auto active = command_bridge_command("active", "context");
  auto handled_active =
      handle_bridge(output, session, session_mutex, active_run_state, active, runtime_options, transport);

  ava::app::rpc::RpcRunState validation_run_state;
  auto plugin_missing = command_bridge_command("plugin-missing", "validate_plugin");
  auto handled_plugin =
      handle_bridge(output, session, session_mutex, validation_run_state, plugin_missing, runtime_options, transport);

  auto mcp_missing = command_bridge_command("mcp-missing", "inspect_mcp_server");
  auto handled_mcp =
      handle_bridge(output, session, session_mutex, validation_run_state, mcp_missing, runtime_options, transport);

  ava::app::rpc::RpcRunState compact_run_state;
  session.model.provider_id = "not-registered";
  auto compact = command_bridge_command("compact-provider", "compact");
  auto handled_compact =
      handle_bridge(output, session, session_mutex, compact_run_state, compact, runtime_options, transport, "openai");
  auto const jsonl = stream.str();

  expect(handled_active && handled_plugin && handled_mcp && handled_compact,
         "RPC command bridge writes expected error responses");
  expect(!ava::app::rpc::active_run(compact_run_state),
         "RPC command bridge clears compact active-run state after startup errors");
  expect(jsonl.find("RPC command is unavailable while a prompt is active") != std::string::npos &&
             jsonl.find("validate_plugin requires path") != std::string::npos &&
             jsonl.find("inspect_mcp_server requires server_id") != std::string::npos &&
             jsonl.find("provider is not registered") != std::string::npos,
         "RPC command bridge reports active-run, validation, and compact provider errors");
}

}  // namespace

void run_app_rpc_command_tests()
{
  test_rpc_command_bridge_context_and_classification();
  test_rpc_command_bridge_errors_and_compact_cleanup();
}
