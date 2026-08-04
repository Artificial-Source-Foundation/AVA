#include "sys.h"
#include "tests/support/app_runtime_support.h"
#include "tests/support/fake_transport.h"
#include "tests/support/runtime_event_test_support.h"
#include "tests/support/test_harness.h"
#include "ava/event/events.h"
#include "ava/http/transport.h"
#include "ava/observability/run_observer.h"
#include "ava/app/clipboard_image.h"
#include "ava/app/interactive_run_queue.h"
#include "ava/app/onboarding.h"
#include "ava/app/rpc/serialization.h"
#include "ava/app/runtime.h"
#include "ava/app/runtime/OpenContext.h"
#include "ava/app/runtime/RunOptions.h"
#include "ava/app/runtime/Session.h"
#include "ava/app/runtime_retry.h"
#include "ava/agent/mode.h"
#include "ava/plugin/diagnostics.h"
#include "ava/plugin/enablement.h"
#include "ava/plugin/tool_broker.h"
#include "ava/mcp/config.h"
#include "ava/mcp/tool_broker.h"
#include "ava/config/auth.h"
#include "ava/session/attachments.h"
#include "ava/session/record.h"
#include "ava/session/session_store.h"
#include "ava/permissions/permission.h"
#include "ava/provider/openai_provider.h"
#include "ava/provider/provider.h"
#include "ava/core/error.h"
#include "ava/core/result.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>
#ifndef AVA_FAKE_MCP_SERVER_PATH
#define AVA_FAKE_MCP_SERVER_PATH ""
#endif

namespace ava::tests::app_runtime_tests {

using namespace ava::tests;

class RuntimeTraceClock final : public ava::observability::Clock
{
 public:
  [[nodiscard]] std::int64_t now_ms() override { return next_.fetch_add(1, std::memory_order_relaxed); }

 private:
  std::atomic<std::int64_t> next_ = 1;
};

class RuntimeTraceCollector final : public ava::observability::RunObserver
{
 public:
  void on_event(ava::observability::TraceEvent const& event) override
  {
    std::lock_guard lock(mutex);
    events.push_back(event);
  }

  std::mutex mutex;
  std::vector<ava::observability::TraceEvent> events;
};

std::string app_tiny_png_bytes()
{
  std::string bytes;
  bytes.push_back(static_cast<char>(0x89));
  bytes += "PNG\r\n";
  bytes.push_back(static_cast<char>(0x1A));
  bytes += "\nava-runtime-image";
  return bytes;
}

std::string ambient_extension_plugin_manifest_json()
{
  return R"({
  "schema_version": 1,
  "id": "com.example.acp-isolation",
  "name": "ACP isolation canary",
  "version": "0.1.0",
  "api_version": "ava.plugin.v1",
  "description": "ACP ambient extension isolation fixture",
  "entrypoint": {"command": "/bin/false", "args": []},
  "capabilities": ["tools"],
  "contributes": {
    "tools": [{"name": "ambient_probe", "description": "ACP_PLUGIN_TOOL_CANARY_92f1", "input_schema": {"type": "object", "additionalProperties": false}}],
    "prompts": [{"name": "ambient-prompt", "description": "Ambient prompt", "path": "prompts/ambient.md"}],
    "skills": [{"name": "ambient-plugin-skill", "description": "ACP_PLUGIN_SKILL_CANARY_1b64", "path": "skills/ambient.md"}]
  }
})";
}

void test_app_run_prompt_isolates_ambient_extensions()
{
  auto const root = create_empty_root("app-runtime-ambient-extension-isolation");
  auto const workspace = root / "workspace";
  auto const home = root / "home";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  std::filesystem::create_directories(home);

  ScopedEnvVar home_env("HOME", home.string());
  ScopedEnvVar config_env("XDG_CONFIG_HOME", paths.config_home.string());
  ScopedEnvVar state_env("XDG_STATE_HOME", paths.state_home.string());
  ScopedEnvVar data_env("XDG_DATA_HOME", paths.data_home.string());

  auto trusted = ava::app::set_project_trust_decision(paths, workspace, true);
  expect(trusted.has_value(), "ambient extension isolation fixture trusts its synthetic workspace");

  write_app_test_file(workspace / "AGENTS.md", "ACP_ORDINARY_CONTEXT_CANARY_846d\n");
  write_app_test_file(workspace / ".ava" / "skills" / "ordinary-canary" / "SKILL.md",
                      "---\nname: ordinary-canary\ndescription: ACP_ORDINARY_SKILL_CANARY_193c\n---\nOrdinary skill body.\n");
  write_app_test_file(workspace / ".ava" / "agents" / "ambient-agent.md",
                      "---\nname: ambient-agent-canary\ndescription: ACP_SUBAGENT_CANARY_629e\nmode: subagent\n---\nCustom agent prompt.\n");
  auto const global_skill_name = "ambient-global-skill";
  write_app_test_file(paths.ava_config_dir / "skills" / global_skill_name / "SKILL.md",
                      "---\nname: ambient-global-skill\ndescription: ACP_GLOBAL_SKILL_CANARY_73c2\n---\nACP_GLOBAL_SKILL_BODY_CANARY_8a91\n");

  auto const plugin_id = "com.example.acp-isolation";
  auto const plugin_dir = paths.ava_config_dir / "plugins" / plugin_id;
  write_app_test_file(plugin_dir / "plugin.json", ambient_extension_plugin_manifest_json());
  write_app_test_file(plugin_dir / "prompts" / "ambient.md", "ACP_PLUGIN_PROMPT_CANARY_e8ad\n");
  write_app_test_file(plugin_dir / "skills" / "ambient.md", "ACP_PLUGIN_SKILL_BODY_CANARY_a53f\n");
  auto enabled = ava::plugin::set_plugin_enabled(paths.ava_state_dir / "plugin-enablement.json", workspace, plugin_id, true, ava::plugin::PluginScope::Global);
  expect(enabled.has_value(), "ambient extension isolation fixture enables its synthetic global plugin");

  auto const ambient_mcp_name = ava::mcp::mcp_model_tool_name("ambient_canary", "echo");
  write_app_test_file(paths.ava_config_dir / "mcp.json", app_test_mcp_config_json("ambient_canary", "Ambient MCP canary", AVA_FAKE_MCP_SERVER_PATH));

  auto diagnostics = ava::plugin::collect_plugin_diagnostics(
      ava::plugin::PluginDiscoveryOptions{.global_plugins_dir = paths.ava_config_dir / "plugins", .project_plugins_dir = workspace / ".ava" / "plugins"},
      paths.ava_state_dir / "plugin-enablement.json", workspace);
  bool const plugin_diagnostic_enabled = std::ranges::any_of(
      diagnostics.plugins, [plugin_id](ava::plugin::PluginStatus const& status) { return status.plugin.manifest.id == plugin_id && status.enabled; });
  expect(plugin_diagnostic_enabled, "ambient extension isolation fixture diagnostics report its plugin enabled");

  ava::app::runtime::OpenContext open_context;
  open_context.workspace_dir = workspace;
  open_context.current_dir = workspace;
  open_context.mode = ava::agent::Mode::Build;
  open_context.paths = paths;
  open_context.prompt_overrides.system_prompt = "ACP_BASE_PROMPT_CANARY_5fa7";
  auto unlocked_session_result = ava::app::runtime::Session::open(open_context);
  expect(unlocked_session_result.has_value(), "runtime opens the ambient extension isolation fixture");
  if (!unlocked_session_result)
    return;
  ava::app::runtime::session_ts& unlocked_session(*unlocked_session_result);

  CRITICAL_AREA_BEGIN_W(session);
  auto const ordinary_prompt = session_w->system_prompt();
  expect(ordinary_prompt.find("ACP_BASE_PROMPT_CANARY_5fa7") != std::string::npos &&
             ordinary_prompt.find("ACP_ORDINARY_CONTEXT_CANARY_846d") != std::string::npos &&
             ordinary_prompt.find("ACP_PLUGIN_PROMPT_CANARY_e8ad") != std::string::npos &&
             ordinary_prompt.find("ACP_PLUGIN_SKILL_CANARY_1b64") != std::string::npos &&
             ordinary_prompt.find("ACP_GLOBAL_SKILL_CANARY_73c2") != std::string::npos &&
             ordinary_prompt.find("ACP_ORDINARY_SKILL_CANARY_193c") != std::string::npos &&
             ordinary_prompt.find("ACP_SUBAGENT_CANARY_629e") != std::string::npos && ordinary_prompt.find("<available_skills>") != std::string::npos &&
             ordinary_prompt.find("<available_subagents>") != std::string::npos,
         "ordinary runtime prompt retains base, context, plugin, skill, and subagent extension canaries");

  session_w->resources().mcp_config = std::make_shared<ava::mcp::McpConfig const>();
  CRITICAL_AREA_END_W(session);

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({ava::http::HttpResponse{
      .status_code = 200,
      .headers = {},
      .body = "data: {\"type\":\"response.output_text.delta\",\"delta\":\"isolated answer\"}\n\n"
              "data: [DONE]\n\n",
  }});
  ava::app::runtime::RunOptions run_options;
  run_options.access_token = "fake";
  run_options.isolate_ambient_extensions = true;
  run_options.exact_builtin_tool_names = std::vector<std::string>{"read_file", "list_directory"};
  auto result = ava::app::run_prompt(unlocked_session, "isolated prompt", provider, transport, run_options);
  expect(result && result->final_text == "isolated answer", "ambient-extension-free runtime prompt completes through the fake transport");
  if (!result || transport.requests().size() != 1)
    return;

  auto const& request = transport.requests()[0].body;
  expect(request.find("ACP_BASE_PROMPT_CANARY_5fa7") != std::string::npos && request.find("ACP_ORDINARY_CONTEXT_CANARY_846d") != std::string::npos,
         "isolated runtime request preserves explicit base prompt and ordinary AGENTS context");
  expect(request.find("ACP_PLUGIN_PROMPT_CANARY_e8ad") == std::string::npos && request.find("ACP_PLUGIN_SKILL_CANARY_1b64") == std::string::npos &&
             request.find("ACP_PLUGIN_SKILL_BODY_CANARY_a53f") == std::string::npos && request.find(global_skill_name) == std::string::npos &&
             request.find("ACP_GLOBAL_SKILL_CANARY_73c2") == std::string::npos && request.find("ACP_GLOBAL_SKILL_BODY_CANARY_8a91") == std::string::npos,
         "isolated runtime request omits plugin and global skill names, catalogs, and bodies");
  expect(request.find("ACP_ORDINARY_SKILL_CANARY_193c") == std::string::npos && request.find("<available_skills>") == std::string::npos,
         "isolated runtime request omits ordinary available-skill catalogs");
  expect(request.find("ACP_SUBAGENT_CANARY_629e") == std::string::npos && request.find("ambient-agent-canary") == std::string::npos &&
             request.find("<available_subagents>") == std::string::npos,
         "isolated runtime request omits builtin and custom subagent catalogs");
  expect(request.find(ava::plugin::plugin_model_tool_name(plugin_id, "ambient_probe")) == std::string::npos,
         "isolated runtime request omits enabled ambient plugin model tools");
  expect(request.find(ambient_mcp_name) == std::string::npos,
         "isolated runtime request does not fall back to ambient MCP when session MCP is explicitly empty");
  expect(request.find("\"name\":\"read_file\"") != std::string::npos && request.find("\"name\":\"list_directory\"") != std::string::npos &&
             request.find("\"name\":\"write_file\"") == std::string::npos,
         "isolated runtime exact composition preserves requested builtins and omits non-exact builtins");

  ava::app::runtime::session_ts::wat(unlocked_session)->resources().mcp_config.reset();
  ava::tests::FakeTransport generic_transport({
      ava::http::HttpResponse{
          .status_code = 200,
          .headers = {},
          .body = "data: {\"type\":\"response.function_call.added\",\"call_id\":\"call_global_skill\",\"name\":\"skill\"}\n\n"
                  "data: {\"type\":\"response.function_call_arguments.delta\",\"call_id\":\"call_global_skill\","
                  "\"delta\":\"{\\\"name\\\":\\\"ambient-global-skill\\\"}\"}\n\n"
                  "data: [DONE]\n\n",
      },
      ava::http::HttpResponse{
          .status_code = 200,
          .headers = {},
          .body = "data: {\"type\":\"response.function_call.added\",\"call_id\":\"call_plugin_skill\",\"name\":\"skill\"}\n\n"
                  "data: {\"type\":\"response.function_call_arguments.delta\",\"call_id\":\"call_plugin_skill\","
                  "\"delta\":\"{\\\"name\\\":\\\"ambient-plugin-skill\\\"}\"}\n\n"
                  "data: [DONE]\n\n",
      },
      ava::http::HttpResponse{
          .status_code = 200,
          .headers = {},
          .body = "data: {\"type\":\"response.output_text.delta\",\"delta\":\"generic isolated answer\"}\n\n"
                  "data: [DONE]\n\n",
      },
  });
  ava::app::runtime::RunOptions generic_options;
  generic_options.access_token = "fake";
  generic_options.isolate_ambient_extensions = true;
  generic_options.permission_resolver = [](ava::permissions::PermissionPrompt const&) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
    return ava::permissions::PermissionResolution::Allow;
  };
  auto generic_result = ava::app::run_prompt(unlocked_session, "generic isolated prompt", provider, generic_transport, generic_options);
  expect(generic_result && generic_result->final_text == "generic isolated answer" && generic_transport.requests().size() == 3,
         "generic ambient isolation with null session MCP completes successive unavailable global and plugin skill calls");
  for (auto const& isolated_request : generic_transport.requests())
  {
    expect(isolated_request.body.find(ambient_mcp_name) == std::string::npos &&
               isolated_request.body.find("ACP_GLOBAL_SKILL_CANARY_73c2") == std::string::npos &&
               isolated_request.body.find("ACP_GLOBAL_SKILL_BODY_CANARY_8a91") == std::string::npos &&
               isolated_request.body.find("ACP_PLUGIN_SKILL_CANARY_1b64") == std::string::npos &&
               isolated_request.body.find("ACP_PLUGIN_SKILL_BODY_CANARY_a53f") == std::string::npos,
           "every generic isolated request omits ambient MCP schema and global/plugin skill catalog and body canaries");
  }
  if (!generic_transport.requests().empty())
  {
    expect(generic_transport.requests().front().body.find(global_skill_name) == std::string::npos &&
               generic_transport.requests().front().body.find("ambient-plugin-skill") == std::string::npos,
           "generic isolated initial request omits ambient global and plugin skill names");
    expect(generic_transport.requests().front().body.find(R"("name":"task")") != std::string::npos &&
               generic_transport.requests().front().body.find(R"("name":"job")") != std::string::npos,
           "generic ambient isolation preserves compiled-in task and job product capabilities");
  }

  ava::tests::FakeTransport exact_null_transport({ava::http::HttpResponse{
      .status_code = 200,
      .headers = {},
      .body = "data: {\"type\":\"response.output_text.delta\",\"delta\":\"exact null answer\"}\n\n"
              "data: [DONE]\n\n",
  }});
  ava::app::runtime::RunOptions exact_null_options;
  exact_null_options.access_token = "fake";
  exact_null_options.isolate_ambient_extensions = true;
  exact_null_options.exact_builtin_tool_names = std::vector<std::string>{"read_file"};
  auto exact_null_result = ava::app::run_prompt(unlocked_session, "exact null MCP prompt", provider, exact_null_transport, exact_null_options);
  expect(exact_null_result && exact_null_result->final_text == "exact null answer" && exact_null_transport.requests().size() == 1,
         "exact isolated composition allocates an empty immutable MCP config when the session config is null");
  expect(ava::app::runtime::session_ts::rat(unlocked_session)->system_prompt() == ordinary_prompt,
         "isolated runtime requests leave the ordinary session system prompt unchanged");
}

void test_app_run_prompt_sources_private_launch_display_from_runtime_invocation()
{
  auto const root = create_empty_root("app-runtime-private-launch-source");
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  std::filesystem::create_directories(paths.ava_config_dir);
  write_app_test_file(paths.models_file, R"JSON({
    "default_provider":"openai",
    "default_model":"launch-source",
    "models":[{
      "provider":"openai",
      "id":"launch-source",
      "name":"Configured   Launch Model",
      "family":"launch-source",
      "api_family":"openai_responses",
      "context_window_tokens":100000,
      "max_output_tokens":4096,
      "supports_tools":true,
      "supports_streaming":true,
      "supports_reasoning":true,
      "reasoning_levels":["high"],
      "input_modalities":["text"],
      "output_modalities":["text"]
    }]
  })JSON");

  ava::app::runtime::OpenContext open_context;
  open_context.workspace_dir = workspace;
  open_context.current_dir = workspace;
  open_context.mode = ava::agent::Mode::Build;
  open_context.paths = paths;
  auto unlocked_session = ava::app::runtime::Session::open(open_context);
  expect(unlocked_session.has_value(), "runtime private-launch source fixture opens a configured session");
  if (!unlocked_session)
    return;
  ava::app::runtime::session_ts::wat session_w(*unlocked_session);
  auto* session = &*session_w;

  auto task_response = [](std::string_view call_id) {
    return ava::http::HttpResponse{.status_code = 200,
                                   .headers = {},
                                   .body = "data: {\"type\":\"response.function_call.added\",\"call_id\":\"" + std::string(call_id) +
                                           "\",\"name\":\"task\"}\n\n"
                                           "data: {\"type\":\"response.function_call_arguments.delta\",\"call_id\":\"" +
                                           std::string(call_id) +
                                           "\",\"delta\":\"{\\\"description\\\":\\\"runtime launch\\\",\\\"prompt\\\":\\\"return child\\\","
                                           "\\\"subagent_type\\\":\\\"general\\\"}\"}\n\n"
                                           "data: {\"type\":\"response.function_call.done\",\"call_id\":\"" +
                                           std::string(call_id) + "\"}\n\ndata: [DONE]\n\n"};
  };
  auto text_response = [](std::string_view text) {
    return ava::http::HttpResponse{.status_code = 200,
                                   .headers = {},
                                   .body = "data: {\"type\":\"response.output_text.delta\",\"delta\":\"" + std::string(text) + "\"}\n\ndata: [DONE]\n\n"};
  };
  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport default_transport({task_response("runtime_task_default"), text_response("parent default")});
  std::vector<ava::agent::SubagentLaunchNotification> launches;
  std::vector<ava::event::RuntimeEvent> public_events;
  std::vector<ava::event::EventEnvelope> public_task_running_envelopes;
  ava::app::InteractiveRunQueue tui_turns(session->store.session_id(), "tui-request-initial", nullptr);
  auto const initial_request_id = tui_turns.active_request_id();
  ava::event::EventEnvelopeContext tui_envelope_context;
  tui_envelope_context.request_id = initial_request_id;
  tui_envelope_context.correlation_id = initial_request_id;
  ava::app::runtime::RunOptions run_options;
  run_options.access_token = "token";
  run_options.request_id = initial_request_id;
  run_options.on_subagent_launch = [&launches](auto const& launch) { launches.push_back(launch); };
  run_options.event_sink = [&public_events, &public_task_running_envelopes, &tui_envelope_context](auto const& event) {
    public_events.push_back(event);
    if (auto const* start = std::get_if<ava::event::ToolStartEvent>(&event.payload()); start && start->payload.tool == "task")
      public_task_running_envelopes.push_back(ava::event::to_event_envelope(event, tui_envelope_context));
    return ava::core::VoidResult{};
  };
  auto default_result = ava::app::run_prompt(*session, "launch default", provider, default_transport, run_options);
  auto selected = session->set_reasoning(ava::app::runtime::ReasoningSelection{.level = "high", .budget_tokens = std::nullopt, .display = {}});
  auto queued = tui_turns.queue_follow_up("launch high");
  auto follow_up = tui_turns.take_next_follow_up();
  auto started = follow_up ? tui_turns.mark_follow_up_started(*follow_up) : ava::core::VoidResult{};
  expect(queued.has_value() && follow_up.has_value() && started.has_value(),
         "runtime private-launch source fixture activates a production TUI queued follow-up identity");
  if (follow_up)
  {
    tui_envelope_context.request_id = follow_up->request_id;
    tui_envelope_context.correlation_id = follow_up->request_id;
  }
  run_options.request_id = follow_up ? follow_up->request_id : std::string("missing-follow-up");
  ava::tests::FakeTransport high_transport({task_response("runtime_task_high"), text_response("parent high")});
  auto high_result = selected ? ava::app::run_prompt(*session, "launch high", provider, high_transport, run_options)
                              : ava::core::Result<ava::agent::AgentLoopResult>(std::unexpected(selected.error()));

  expect(default_result && high_result, "runtime private-launch source fixture completes both delegated turns: default=" +
                                            (default_result ? std::string("ok") : default_result.error().format()) +
                                            ", high=" + (high_result ? std::string("ok") : high_result.error().format()));
  expect(launches.size() == 2, "runtime private-launch source fixture emits exactly two callbacks; actual=" + std::to_string(launches.size()) +
                                   ", requests=" + std::to_string(default_transport.requests().size() + high_transport.requests().size()) +
                                   ", default_text=" + (default_result ? default_result->final_text : std::string("error")) +
                                   ", high_text=" + (high_result ? high_result->final_text : std::string("error")) +
                                   (launches.empty() ? std::string{} : ", first_id=" + launches.front().tool_call_id));
  if (launches.size() == 2)
  {
    expect(launches[0].tool_call_id == "runtime_task_default" && launches[1].tool_call_id == "runtime_task_high",
           "runtime private launch callback captures exact backend task call identities");
    expect(launches[0].request_id == initial_request_id && launches[0].correlation_id == initial_request_id && follow_up &&
               launches[1].request_id == follow_up->request_id && launches[1].correlation_id == follow_up->request_id &&
               launches[1].correlation_id != launches[0].correlation_id,
           "runtime private launch callback captures initial and queued TUI request identities");
    expect(public_task_running_envelopes.size() == 2 && public_task_running_envelopes[0].request_id == launches[0].request_id &&
               public_task_running_envelopes[0].correlation_id == launches[0].correlation_id &&
               public_task_running_envelopes[1].request_id == launches[1].request_id &&
               public_task_running_envelopes[1].correlation_id == launches[1].correlation_id,
           "initial and queued TUI turns give public task Running envelopes and private launch notifications exactly matching identities");
    expect(launches[0].display.model_display_name() == "Configured Launch Model" && launches[1].display.model_display_name() == "Configured Launch Model",
           "runtime private launch display uses configured ModelInfo display name; actual=" + launches[0].display.model_display_name());
    expect(launches[0].display.reasoning_label() == "default" && launches[1].display.reasoning_label() == "high",
           "runtime private launch display uses literal default then explicit AVA level; actual=" + launches[0].display.reasoning_label() + "/" +
               launches[1].display.reasoning_label());
  }

  std::string public_event_bytes;
  std::size_t event_index = 0;
  for (auto const& event : public_events)
  {
    ava::event::EventEnvelopeContext context;
    context.event_id = "event_" + std::to_string(event_index++);
    public_event_bytes += ava::event::serialize_event_envelope_jsonl(ava::event::to_event_envelope(event, context));
  }
  auto const session_bytes = app_read_binary_file(session->store.session_path());
  expect(public_event_bytes.find("Configured Launch Model") == std::string::npos,
         "private launch model display stays absent from public runtime event envelopes");
  expect(session_bytes.find("Configured Launch Model") == std::string::npos, "private launch model display stays absent from persisted session bytes");

  write_app_test_file(paths.models_file, R"JSON({
    "default_provider":"openai",
    "default_model":"unnamed-launch-model",
    "models":[{
      "provider":"openai",
      "id":"unnamed-launch-model",
      "family":"unnamed-launch-model",
      "api_family":"openai_responses",
      "supports_tools":true,
      "supports_streaming":true,
      "input_modalities":["text"],
      "output_modalities":["text"]
    }]
  })JSON");
  auto unlocked_unnamed_session = ava::app::runtime::Session::open(open_context);
  expect(unlocked_unnamed_session.has_value(), "runtime private-launch source fixture opens an unnamed custom model session");
  if (unlocked_unnamed_session)
  {
    ava::app::runtime::session_ts::wat unnamed_session_w(*unlocked_unnamed_session);
    auto& unnamed_session = *unnamed_session_w;
    std::vector<ava::agent::SubagentLaunchNotification> unnamed_launches;
    ava::app::runtime::RunOptions unnamed_options;
    unnamed_options.access_token = "token";
    unnamed_options.request_id = std::nullopt;
    unnamed_options.on_subagent_launch = [&unnamed_launches](auto const& launch) { unnamed_launches.push_back(launch); };
    ava::tests::FakeTransport unnamed_transport({task_response("runtime_task_unnamed"), text_response("parent unnamed")});
    auto unnamed_result = ava::app::run_prompt(unnamed_session, "launch unnamed", provider, unnamed_transport, unnamed_options);
    expect(unnamed_result && unnamed_launches.size() == 1 && unnamed_session.model().display_name == "unnamed-launch-model" &&
               ava::config::proven_configured_model_display_name(unnamed_session.model()).empty() &&
               unnamed_launches.front().display.model_display_name().empty() && !unnamed_launches.front().request_id.empty() &&
               unnamed_launches.front().request_id == unnamed_launches.front().correlation_id,
           "direct run_prompt generates and consistently uses one admitted request identity while unnamed custom launch display never falls back to ids");
  }
}

void test_app_run_prompt_emits_events()
{
  auto const root = create_empty_root("app-runtime-run");

  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  {
    std::ofstream file(workspace / "AGENTS.md", std::ios::binary | std::ios::trunc);
    file << "runtime run context\n";
  }

  ava::app::runtime::OpenContext open_context;
  open_context.workspace_dir = workspace;
  open_context.current_dir = workspace;
  open_context.mode = ava::agent::Mode::Build;
  open_context.paths = paths;
  auto unlocked_session_result = ava::app::runtime::Session::open(open_context);
  expect(unlocked_session_result.has_value(), "runtime run test opens session");
  if (!unlocked_session_result)
    return;
  ava::app::runtime::session_ts::wat session_w(*unlocked_session_result);

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({ava::http::HttpResponse{
      .status_code = 200,
      .headers = {},
      .body = "data: {\"type\":\"response.output_text.delta\",\"delta\":\"runtime answer\"}\n\n"
              "data: [DONE]\n\n",
  }});
  std::vector<ava::event::RuntimeEvent> events;
  ava::app::runtime::RunOptions run_options;
  run_options.access_token = "token";
  run_options.event_sink = [&events](ava::event::RuntimeEvent const& event) {
    events.push_back(event);
    return ava::core::VoidResult{};
  };
  auto result = ava::app::run_prompt(*session_w, "hello runtime", provider, transport, run_options);
  expect(result && result->final_text == "runtime answer", "runtime run_prompt returns agent loop result");
  auto const* assistant = events.size() == 4 ? ava::tests::runtime_event_as<ava::event::AssistantMessageEvent>(events[2]) : nullptr;
  auto const* completion = events.size() == 4 ? ava::tests::runtime_event_as<ava::event::CompletionEvent>(events[3]) : nullptr;
  expect(events.size() == 4 && ava::tests::runtime_event_as<ava::event::SessionStartEvent>(events[0]) &&
             ava::tests::runtime_event_as<ava::event::UserMessageEvent>(events[1]) && assistant && completion,
         "runtime run_prompt emits session, user, assistant, and done events");
  expect(assistant && assistant->payload.text == "runtime answer" && completion && completion->payload.provider_iterations == 1,
         "runtime run_prompt events include final text and completion counters");
  expect(transport.requests().size() == 1 && transport.requests()[0].body.find("runtime run context") != std::string::npos,
         "runtime run_prompt sends context-augmented system prompt to provider");
  auto entries = session_w->store.load();
  expect(entries && entries->size() == 4 && (*entries)[1].type == ava::session::EntryType::UserMessage &&
             (*entries)[2].type == ava::session::EntryType::AssistantOutputItem && (*entries)[3].type == ava::session::EntryType::AssistantTurnCommit,
         "runtime run_prompt persists one committed v4 assistant turn in the runtime session");
}

void test_app_run_prompt_expands_file_references()
{
  auto const root = create_empty_root("app-runtime-file-reference");

  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace / "src");
  std::filesystem::create_directories(workspace / "my folder");
  {
    std::ofstream file(workspace / "src" / "reference.cpp", std::ios::binary | std::ios::trunc);
    file << "int referenced_symbol() { return 42; }\n";
  }
  {
    std::ofstream file(workspace / "my folder" / "reference file.cpp", std::ios::binary | std::ios::trunc);
    file << "int spaced_reference_symbol() { return 24; }\n";
  }

  ava::app::runtime::OpenContext open_context;
  open_context.workspace_dir = workspace;
  open_context.current_dir = workspace;
  open_context.mode = ava::agent::Mode::Build;
  open_context.paths = paths;
  auto unlocked_session_result = ava::app::runtime::Session::open(open_context);
  expect(unlocked_session_result.has_value(), "runtime file reference test opens session");
  if (!unlocked_session_result)
    return;
  ava::app::runtime::session_ts::wat session_w(*unlocked_session_result);

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({ava::http::HttpResponse{
      .status_code = 200,
      .headers = {},
      .body = "data: {\"type\":\"response.output_text.delta\",\"delta\":\"reference answer\"}\n\n"
              "data: [DONE]\n\n",
  }});
  std::vector<ava::event::RuntimeEvent> events;
  ava::app::runtime::RunOptions run_options;
  run_options.access_token = "token";
  run_options.event_sink = [&events](ava::event::RuntimeEvent const& event) {
    events.push_back(event);
    return ava::core::VoidResult{};
  };
  auto result = ava::app::run_prompt(*session_w, "review @src/reference.cpp and @\"my folder/reference file.cpp\"", provider, transport, run_options);
  expect(result && result->final_text == "reference answer", "runtime file reference prompt succeeds");
  auto const* user_message = events.size() >= 2 ? ava::tests::runtime_event_as<ava::event::UserMessageEvent>(events[1]) : nullptr;
  expect(user_message && user_message->payload.text.find("Referenced files:") != std::string::npos &&
             user_message->payload.text.find("int referenced_symbol()") != std::string::npos &&
             user_message->payload.text.find("int spaced_reference_symbol()") != std::string::npos,
         "runtime user_message event contains expanded plain and quoted file reference content");
  expect(transport.requests().size() == 1 && transport.requests()[0].body.find("review @src/reference.cpp") != std::string::npos &&
             transport.requests()[0].body.find("--- src/reference.cpp ---") != std::string::npos &&
             transport.requests()[0].body.find("int referenced_symbol()") != std::string::npos &&
             transport.requests()[0].body.find("--- my folder/reference file.cpp ---") != std::string::npos &&
             transport.requests()[0].body.find("int spaced_reference_symbol()") != std::string::npos,
         "runtime provider request receives bounded plain and quoted referenced file content");
  auto entries = session_w->store.load();
  auto const expanded_user_entry = entries && std::ranges::any_of(*entries, [](ava::session::SessionEntry const& entry) {
                                     return entry.type == ava::session::EntryType::UserMessage &&
                                            entry.data_json.find("int referenced_symbol()") != std::string::npos &&
                                            entry.data_json.find("int spaced_reference_symbol()") != std::string::npos;
                                   });
  expect(expanded_user_entry, "runtime session persists expanded plain and quoted file reference content in the user entry");
}

void test_app_run_prompt_sends_imported_image_attachment()
{
  auto const root = create_empty_root("app-runtime-image-attachment");

  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  auto const image_path = workspace / "screen.png";
  write_app_test_file(image_path, app_tiny_png_bytes());

  ava::app::runtime::OpenContext open_context;
  open_context.workspace_dir = workspace;
  open_context.current_dir = workspace;
  open_context.mode = ava::agent::Mode::Build;
  open_context.paths = paths;
  auto unlocked_session_result = ava::app::runtime::Session::open(open_context);
  expect(unlocked_session_result.has_value(), "runtime image attachment test opens session");
  if (!unlocked_session_result)
    return;
  ava::app::runtime::session_ts::wat session_w(*unlocked_session_result);

  auto imported = ava::session::import_image_attachment(session_w->store, image_path);
  expect(imported.has_value(), "runtime image attachment test imports local image into session storage");
  if (!imported)
    return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({ava::http::HttpResponse{
      .status_code = 200,
      .headers = {},
      .body = "data: {\"type\":\"response.output_text.delta\",\"delta\":\"image answer\"}\n\n"
              "data: [DONE]\n\n",
  }});
  ava::app::runtime::RunOptions run_options;
  run_options.access_token = "token";
  run_options.image_attachments = {*imported};

  auto result = ava::app::run_prompt(*session_w, "describe this image", provider, transport, run_options);
  expect(result && result->final_text == "image answer", "runtime run_prompt accepts imported image attachments");
  expect(transport.requests().size() == 1 && transport.requests()[0].body.find("\"type\":\"input_image\"") != std::string::npos &&
             transport.requests()[0].body.find("data:image/png;base64,") != std::string::npos,
         "runtime provider request includes a verified image data URL payload");
  auto entries = session_w->store.load();
  auto const persisted_metadata = entries && std::ranges::any_of(*entries, [](ava::session::SessionEntry const& entry) {
                                    return entry.type == ava::session::EntryType::UserMessage && entry.data_json.find("\"attachments\"") != std::string::npos &&
                                           entry.data_json.find("\"mime_type\":\"image/png\"") != std::string::npos &&
                                           entry.data_json.find("data_base64") == std::string::npos;
                                  });
  expect(persisted_metadata, "runtime persists image attachment metadata without inline image bytes");
}

void test_app_clipboard_image_file_override_imports_attachment()
{
  auto const root = create_empty_root("app-runtime-clipboard-image");

  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  auto const image_path = workspace / "clipboard.png";
  write_app_test_file(image_path, app_tiny_png_bytes());

  ava::app::runtime::OpenContext open_context;
  open_context.workspace_dir = workspace;
  open_context.current_dir = workspace;
  open_context.mode = ava::agent::Mode::Build;
  open_context.paths = paths;
  auto unlocked_session_result = ava::app::runtime::Session::open(open_context);
  expect(unlocked_session_result.has_value(), "runtime clipboard image test opens session");
  if (!unlocked_session_result)
    return;
  ava::app::runtime::session_ts::wat session_w(*unlocked_session_result);

  ScopedEnvVar clipboard_file("AVA_CLIPBOARD_IMAGE_FILE", image_path.string());
  auto imported = ava::app::import_clipboard_image_attachment(session_w->store);
  expect(imported && imported->has_value() && (*imported)->mime_type == "image/png" && (*imported)->byte_size == app_tiny_png_bytes().size(),
         "runtime clipboard image override imports supported image bytes into session storage");
  if (!imported || !*imported)
    return;

  auto loaded = ava::session::load_image_attachment(session_w->store, **imported);
  expect(loaded && loaded->bytes == app_tiny_png_bytes(), "runtime clipboard image override writes reusable session-owned attachment bytes");
}

void test_app_run_prompt_emits_provider_retry_events_when_enabled()
{
  auto const root = create_empty_root("app-runtime-provider-retry");

  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::runtime::OpenContext open_context;
  open_context.workspace_dir = workspace;
  open_context.current_dir = workspace;
  open_context.mode = ava::agent::Mode::Build;
  open_context.paths = paths;
  auto unlocked_session_result = ava::app::runtime::Session::open(open_context);
  expect(unlocked_session_result.has_value(), "runtime provider retry test opens session");
  if (!unlocked_session_result)
    return;
  ava::app::runtime::session_ts::wat session_w(*unlocked_session_result);

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport(
      {ava::http::HttpResponse{.status_code = 429, .headers = {{"Retry-After", "0"}}, .body = "{\"error\":{\"message\":\"rate limited\"}}"},
       sse_response(final_text_sse("retried answer"))});
  std::vector<ava::event::RuntimeEvent> events;
  ava::app::runtime::RunOptions run_options;
  run_options.access_token = "token";
  run_options.enable_transport_retries = true;
  run_options.event_sink = [&events](ava::event::RuntimeEvent const& event) {
    events.push_back(event);
    return ava::core::VoidResult{};
  };
  bool runtime_retry_cancel = false;
  run_options.cancel_requested = [&runtime_retry_cancel] { return runtime_retry_cancel; };

  auto result = ava::app::run_prompt(*session_w, "retry runtime", provider, transport, run_options);
  expect(result && result->final_text == "retried answer" && transport.requests().size() == 2,
         "runtime run_prompt retries transient provider transport failures when enabled");
  expect(std::ranges::any_of(events,
                             [](ava::event::RuntimeEvent const& event) {
                               auto const* retry = ava::tests::runtime_event_as<ava::event::RetryEvent>(event);
                               return retry && retry->payload.trigger == "provider_transport" && retry->payload.reason == "rate_limited" &&
                                      retry->payload.attempt == 2 && retry->payload.max_attempts == 3 && retry->payload.delay_ms == 0 &&
                                      retry->payload.text == "HTTP status 429";
                             }),
         "runtime run_prompt emits provider retry metadata through the shared event sink");
  events.clear();
  auto retry_options = ava::app::runtime::runtime_retry_options(*session_w, run_options);
  expect(retry_options.on_retry != nullptr, "runtime retry options expose provider retry event mapping");
  expect(retry_options.response_retry_decision != nullptr, "runtime retry options install provider response retry classification");
  if (retry_options.response_retry_decision)
  {
    expect(retry_options.response_retry_decision(ava::http::HttpResponse{.status_code = 429, .headers = {}, .body = "rate limited"}) ==
               ava::http::ResponseRetryDecision::RateLimited,
           "runtime retry options classify generic 429 responses as rate-limited retries");
    expect(retry_options.response_retry_decision(ava::http::HttpResponse{.status_code = 503, .headers = {}, .body = "try again"}) ==
               ava::http::ResponseRetryDecision::Transient,
           "runtime retry options classify generic 503 responses as transient retries");
    expect(retry_options.response_retry_decision(ava::http::HttpResponse{.status_code = 401, .headers = {}, .body = "unauthorized"}) ==
               ava::http::ResponseRetryDecision::NoRetry,
           "runtime retry options classify authentication failures as non-retryable");
    expect(retry_options.response_retry_decision(ava::http::HttpResponse{
               .status_code = 429, .headers = {}, .body = "insufficient_quota: billing hard limit"}) == ava::http::ResponseRetryDecision::NoRetry,
           "runtime retry options classify 429 quota bodies as non-retryable");
  }
  runtime_retry_cancel = true;
  expect(retry_options.cancel_requested && retry_options.cancel_requested(), "runtime retry options preserve the active run cancellation callback");
  runtime_retry_cancel = false;
  if (retry_options.on_retry)
  {
    auto emitted_tick = retry_options.on_retry(ava::http::RetryOptions::Event{.attempt = 2,
                                                                              .max_attempts = 3,
                                                                              .delay_ms = 1000,
                                                                              .remaining_ms = 500,
                                                                              .reason = "rate_limited",
                                                                              .status_code = 429,
                                                                              .streaming = true,
                                                                              .countdown_tick = true});
    auto const* tick = events.size() == 1 ? ava::tests::runtime_event_as<ava::event::RetryTickEvent>(events[0]) : nullptr;
    expect(emitted_tick.has_value() && tick && tick->payload.trigger == "provider_transport" && tick->payload.remaining_ms == 500 &&
               tick->payload.delay_ms == 1000 && tick->payload.status == "streaming",
           "runtime retry options map provider countdown ticks to explicit backend retry_tick events");
  }
}

void test_app_run_prompt_observation_shares_context_across_compaction_and_retry()
{
  auto const root = create_empty_root("app-runtime-observation-context");

  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::runtime::OpenContext open_context;
  open_context.workspace_dir = workspace;
  open_context.current_dir = workspace;
  open_context.paths = paths;
  auto unlocked_session_result = ava::app::runtime::Session::open(open_context);
  expect(unlocked_session_result.has_value(), "observed runtime compaction test opens session");
  if (!unlocked_session_result)
    return;
  ava::app::runtime::session_ts::wat session_w(*unlocked_session_result);
  session_w->model_selection().model.context_window_tokens = 100;
  auto seeded = session_w->append_owned({.id = "observed-old-context",
                                         .parent_id = "",
                                         .type = ava::session::EntryType::UserMessage,
                                         .timestamp = ava::session::now_timestamp(),
                                         .data_json = "{\"text\":\"" + std::string(420, 'x') + "\"}"});
  expect(seeded.has_value(), "observed runtime compaction test seeds enough context to require a summary request");
  if (!seeded)
    return;

  auto collector = std::make_shared<RuntimeTraceCollector>();
  auto observation = std::make_shared<ava::observability::RunObservation>(collector, std::make_shared<RuntimeTraceClock>(),
                                                                          std::make_shared<ava::observability::CounterIdGenerator>());
  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport(
      {ava::http::HttpResponse{.status_code = 429, .headers = {{"Retry-After", "0"}}, .body = "{\"error\":{\"message\":\"rate limited\"}}"},
       ava::http::HttpResponse{.status_code = 200, .headers = {}, .body = "{\"output_text\":\"OBSERVED SUMMARY\"}"},
       sse_response(final_text_sse("observed answer"))});
  ava::app::runtime::RunOptions run_options;
  run_options.access_token = "CANARY_RUNTIME_TOKEN";
  run_options.enable_transport_retries = true;
  run_options.observation = observation;

  auto result = ava::app::run_prompt(*session_w, "continue observed run", provider, transport, run_options);
  expect(result && result->final_text == "observed answer" && transport.requests().size() == 3,
         "one scripted run_prompt performs a retried compaction summary then its agent request");
  if (!result)
    return;

  std::vector<ava::observability::TraceEvent> trace_events;
  {
    std::lock_guard lock(collector->mutex);
    trace_events = collector->events;
  }
  std::string run_id;
  std::string turn_id;
  unsigned logical_requests = 0;
  unsigned attempt_results = 0;
  bool retry = false;
  bool terminal = false;
  for (auto const& event : trace_events)
  {
    if (run_id.empty())
    {
      run_id = event.run_id;
      turn_id = event.turn_id;
    }
    expect(!event.run_id.empty() && !event.turn_id.empty() && event.run_id == run_id && event.turn_id == turn_id,
           "compaction, retries, attempts, and agent events share one runtime-generated run/turn context");
    logical_requests += event.type == ava::observability::TraceEventType::TransportRequestResult;
    attempt_results += event.type == ava::observability::TraceEventType::TransportAttemptResult;
    retry = retry || event.type == ava::observability::TraceEventType::TransportRetry;
    terminal = terminal || (event.type == ava::observability::TraceEventType::AgentRunTerminal && event.outcome == ava::observability::TraceOutcome::Completed);
  }
  expect(!run_id.empty() && logical_requests == 2 && attempt_results == 3 && retry && terminal,
         "trace contains both logical requests, the summary retry attempts, retry record, and completed agent terminal");

  std::ifstream session_file(session_w->store.session_path(), std::ios::binary);
  std::string session_json((std::istreambuf_iterator<char>(session_file)), std::istreambuf_iterator<char>());
  auto messages_json = session_w->messages_result_json();
  auto prompt_json = ava::app::rpc::prompt_result_json(session_w->store.session_id(), *result);
  auto state_json = session_w->state_result_json_1(false);
  auto has_observer_fields = [](std::string_view json) {
    return json.find("agent.run_start") != std::string_view::npos || json.find("transport.attempt_result") != std::string_view::npos ||
           json.find("\"timestamp_ms\":") != std::string_view::npos || json.find("\"run_id\":") != std::string_view::npos ||
           json.find("CANARY_RUNTIME_TOKEN") != std::string_view::npos;
  };
  expect(messages_json && !has_observer_fields(session_json) && !has_observer_fields(*messages_json) && !has_observer_fields(prompt_json) &&
             !has_observer_fields(state_json),
         "observer-only fields and canaries never enter authoritative session or RPC JSON");
}

void test_app_run_prompt_emits_tool_progress_and_session_spill()
{
  auto const root = create_empty_root("app-runtime-tool-progress");

  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  expect(::chmod(temp_root().c_str(), S_IRWXU) == 0 && ::chmod(root.c_str(), S_IRWXU) == 0 && ::chmod(workspace.c_str(), S_IRWXU) == 0,
         "runtime tool progress workspace is owner-only for sealed command planning");

  ava::app::runtime::OpenContext open_context;
  open_context.workspace_dir = workspace;
  open_context.current_dir = workspace;
  open_context.mode = ava::agent::Mode::Build;
  open_context.paths = paths;
  auto unlocked_session_result = ava::app::runtime::Session::open(open_context);
  expect(unlocked_session_result.has_value(), "runtime tool progress test opens session");
  if (!unlocked_session_result)
    return;
  ava::app::runtime::session_ts::wat session_w(*unlocked_session_result);

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({ava::http::HttpResponse{
                                           .status_code = 200,
                                           .headers = {},
                                           .body = "data: {\"type\":\"response.function_call.added\",\"call_id\":"
                                                   "\"call_bash\",\"name\":\"bash\"}\n\n"
                                                   "data: {\"type\":\"response.function_call_arguments.delta\","
                                                   "\"call_id\":\"call_bash\",\"delta\":\"{\\\"command\\\":"
                                                   "\\\"pwd\\\",\\\"max_bytes\\\":4}\"}\n\n"
                                                   "data: [DONE]\n\n",
                                       },
                                       ava::http::HttpResponse{
                                           .status_code = 200,
                                           .headers = {},
                                           .body = "data: {\"type\":\"response.output_text.delta\",\"delta\":"
                                                   "\"tool done\"}\n\n"
                                                   "data: [DONE]\n\n",
                                       }});
  std::vector<ava::event::RuntimeEvent> events;
  ava::app::runtime::RunOptions run_options;
  run_options.access_token = "token";
  run_options.permission_resolver = [](ava::permissions::PermissionPrompt const&) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
    return ava::permissions::PermissionResolution::Allow;
  };
  run_options.event_sink = [&events](ava::event::RuntimeEvent const& event) {
    events.push_back(event);
    return ava::core::VoidResult{};
  };

  auto result = ava::app::run_prompt(*session_w, "run pwd", provider, transport, run_options);
  auto const spill_dir = session_w->store.session_path().parent_path() / "spill";
  bool has_spill_file = false;
  std::error_code iter_error;
  for (std::filesystem::directory_iterator it(spill_dir, iter_error), end; !iter_error && it != end; it.increment(iter_error))
  {
    has_spill_file = true;
    expect(it->path().parent_path() == spill_dir, "runtime spill file stays under the session-local spill directory");
    break;
  }
  expect(result && result->final_text == "tool done" &&
             std::ranges::any_of(events,
                                 [](ava::event::RuntimeEvent const& event) {
                                   auto const* progress = ava::tests::runtime_event_as<ava::event::ToolProgressEvent>(event);
                                   return progress && progress->payload.call_id == "call_bash" && progress->payload.tool == "bash" &&
                                          !progress->payload.text.empty();
                                 }),
         "runtime run_prompt emits additive tool_progress events from tool callbacks");
  expect(std::ranges::any_of(events,
                             [](ava::event::RuntimeEvent const& event) {
                               auto const* start = ava::tests::runtime_event_as<ava::event::ToolStartEvent>(event);
                               return start && start->payload.call_id == "call_bash" && start->payload.tool == "bash" &&
                                      start->payload.args_json.find("\"command\":\"pwd\"") != std::string::npos;
                             }) &&
             std::ranges::any_of(events,
                                 [](ava::event::RuntimeEvent const& event) {
                                   auto const* result = ava::tests::runtime_event_as<ava::event::ToolResultEvent>(event);
                                   return result && result->payload.call_id == "call_bash" && result->payload.tool == "bash" && result->payload.truncated &&
                                          result->payload.total_bytes > 0 && result->payload.output_lines > 0 && result->payload.total_lines > 0 &&
                                          !result->payload.spill_path.empty() && result->payload.result_json.find("\"spill_file\"") != std::string::npos;
                                 }),
         "runtime run_prompt emits semantic tool args, result, and spill metadata for frontend adapters");
  expect(has_spill_file, "runtime run_prompt configures session-local spill files for truncated tool output");
}

void test_app_first_run_auth_onboarding()
{
  auto const root = create_empty_root("app-first-run-onboarding");

  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  auto const home = root / "home";
  std::filesystem::create_directories(workspace);
  std::filesystem::create_directories(paths.ava_config_dir);
  auto const home_text = home.string();
  auto const config_home_text = paths.config_home.string();
  auto const state_home_text = paths.state_home.string();
  auto const data_home_text = paths.data_home.string();
  setenv("HOME", home_text.c_str(), 1);
  setenv("XDG_CONFIG_HOME", config_home_text.c_str(), 1);
  setenv("XDG_STATE_HOME", state_home_text.c_str(), 1);
  setenv("XDG_DATA_HOME", data_home_text.c_str(), 1);

  ava::app::runtime::OpenContext open_context;
  open_context.workspace_dir = workspace;
  open_context.current_dir = workspace;
  open_context.mode = ava::agent::Mode::Build;
  open_context.paths = paths;
  auto unlocked_session_result = ava::app::runtime::Session::open(open_context);
  expect(unlocked_session_result.has_value(), "first-run onboarding test opens session");
  if (!unlocked_session_result)
    return;
  ava::app::runtime::session_ts::wat session_w(*unlocked_session_result);

  unsetenv("OPENAI_API_KEY");
  auto missing = ava::app::first_run_auth_onboarding_message(*session_w);
  expect(missing && *missing == "! OpenAI not connected · /connect" && missing->find(paths.auth_file.string()) == std::string::npos &&
             std::ranges::count(*missing, '\n') == 0,
         "first-run TUI onboarding is one actionable advisory row without auth paths or environment dumps");

  auto required = ava::app::provider_auth_required_message(*session_w, "\nslash tool commands still work offline.");
  expect(required.find("Auth is required for provider `openai`") != std::string::npos &&
             required.find("Connect with /connect or /login") != std::string::npos &&
             required.find("slash tool commands still work offline") != std::string::npos,
         "provider auth failure reuses onboarding guidance before a prompt runs");

  setenv("OPENAI_API_KEY", "test-openai-key", 1);
  auto env_ready = ava::app::first_run_auth_onboarding_message(*session_w);
  expect(!env_ready, "first-run onboarding stays hidden when provider auth comes from the environment");
  unsetenv("OPENAI_API_KEY");

  auto stored = ava::config::store_provider_credential(
      paths, ava::config::ProviderCredential{.provider_id = "openai", .access_token = "stored-openai-key", .credential_type = "api_key", .source = "test"});
  expect(stored.has_value(), "first-run onboarding test stores OpenAI credential");
  auto stored_ready = ava::app::first_run_auth_onboarding_message(*session_w);
  expect(!stored_ready, "first-run onboarding stays hidden when provider auth is stored");
}

void test_app_run_prompt_event_sink_failure_cancels_before_next_provider_call()
{
  auto const root = create_empty_root("app-runtime-event-sink-cancel");

  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  {
    std::ofstream file(workspace / "note.txt", std::ios::binary | std::ios::trunc);
    file << "event sink cancel data";
  }

  ava::app::runtime::OpenContext open_context;
  open_context.workspace_dir = workspace;
  open_context.current_dir = workspace;
  open_context.mode = ava::agent::Mode::Build;
  open_context.paths = paths;
  auto unlocked_session_result = ava::app::runtime::Session::open(open_context);
  expect(unlocked_session_result.has_value(), "runtime event sink failure test opens session");
  if (!unlocked_session_result)
    return;
  ava::app::runtime::session_ts& unlocked_session = *unlocked_session_result;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({ava::http::HttpResponse{
                                           .status_code = 200,
                                           .headers = {},
                                           .body = "data: {\"type\":\"response.function_call.added\",\"call_id\":"
                                                   "\"call_read\",\"name\":\"read_file\"}\n\n"
                                                   "data: "
                                                   "{\"type\":\"response.function_call_arguments.delta\","
                                                   "\"call_id\":\"call_read\",\"delta\":\"{\\\"path\\\":"
                                                   "\\\"note.txt\\\"}\"}\n\n"
                                                   "data: [DONE]\n\n",
                                       },
                                       ava::http::HttpResponse{
                                           .status_code = 200,
                                           .headers = {},
                                           .body = "data: {\"type\":\"response.output_text.delta\",\"delta\":"
                                                   "\"should not request\"}\n\n"
                                                   "data: [DONE]\n\n",
                                       }});
  ava::app::runtime::RunOptions run_options;
  run_options.access_token = "token";
  run_options.event_sink = [](ava::event::RuntimeEvent const& event) {
    if (event.type() == ava::event::RuntimeEventType::ToolStart)
    {
      return ava::core::VoidResult{std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "event sink failed"))};
    }
    return ava::core::VoidResult{};
  };

  auto result = ava::app::run_prompt(unlocked_session, "read with failing sink", provider, transport, run_options);
  expect(!result && result.error().category() == ava::core::ErrorCategory::Io && result.error().message() == "event sink failed",
         "runtime returns the event sink write failure");
  expect(transport.requests().size() == 1, "event sink failure cancels before the next provider request");
}

}  // namespace ava::tests::app_runtime_tests
