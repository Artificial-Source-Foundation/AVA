#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "ava/agent/tool_dispatcher.h"
#include "ava/core/json.h"
#include "ava/mcp/config.h"
#include "ava/mcp/stdio_client.h"
#include "ava/mcp/tool_broker.h"
#include "ava/permissions/permission.h"
#include "ava/tools/file_tools.h"
#include "tests/support/test_harness.h"

#ifndef AVA_FAKE_MCP_SERVER_PATH
#define AVA_FAKE_MCP_SERVER_PATH ""
#endif

namespace {

void write_text(const std::filesystem::path& path, const std::string& text) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file << text;
}

std::string mcp_config_json(std::string id, std::string command, bool enabled = true, std::string args_json = "[]") {
  return std::string("{\"servers\":[{\"id\":\"") + ava::core::json::escape(id) +
         "\",\"name\":\"Demo MCP\",\"command\":\"" + ava::core::json::escape(command) + "\",\"args\":" + args_json +
         ",\"enabled\":" + (enabled ? "true" : "false") + "}]}";
}

ava::mcp::McpServerConfig fake_server_config(const std::filesystem::path& root) {
  return ava::mcp::McpServerConfig{.id = "demo",
                                   .name = "Demo MCP",
                                   .command = AVA_FAKE_MCP_SERVER_PATH,
                                   .args = {},
                                   .enabled = true,
                                   .scope = ava::mcp::McpServerScope::Project,
                                   .source_path = root / "mcp.json"};
}

ava::mcp::McpStdioClientOptions fake_client_options(const std::filesystem::path& workspace) {
  ava::mcp::McpStdioClientOptions options;
  options.workspace_dir = workspace;
  options.startup_timeout = std::chrono::milliseconds(500);
  options.request_timeout = std::chrono::milliseconds(500);
  return options;
}

void test_mcp_config_parsing() {
  auto global =
      ava::mcp::parse_mcp_config("{\"servers\":[{\"id\":\"demo\",\"command\":\"/bin/demo\",\"args\":[\"--stdio\"]}]}",
                                 "/tmp/global-mcp.json", ava::mcp::McpServerScope::Global);
  expect(global && global->servers.size() == 1 && global->servers[0].enabled && global->servers[0].args.size() == 1 &&
             global->servers[0].scope == ava::mcp::McpServerScope::Global,
         global ? "MCP config parses global server defaults"
                : "MCP config parses global server defaults: " + global.error().format());

  auto project = ava::mcp::parse_mcp_config("{\"servers\":[{\"id\":\"demo-project\",\"command\":\"/bin/demo\"}]}",
                                            "/tmp/project-mcp.json", ava::mcp::McpServerScope::Project);
  expect(project && project->servers.size() == 1 && !project->servers[0].enabled,
         project ? "MCP project config servers default to disabled"
                 : "MCP project config servers default to disabled: " + project.error().format());

  auto bad_id = ava::mcp::parse_mcp_config("{\"servers\":[{\"id\":\"bad id\",\"command\":\"demo\"}]}",
                                           "/tmp/bad-mcp.json", ava::mcp::McpServerScope::Global);
  expect(!bad_id && bad_id.error().message().find("valid id") != std::string::npos,
         "MCP config rejects invalid server ids");

  const auto root = temp_root() / "mcp-config";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto global_path = root / "global" / "mcp.json";
  const auto project_path = root / "workspace" / ".ava" / "mcp.json";
  write_text(global_path, mcp_config_json("same", "/bin/demo"));
  write_text(project_path, mcp_config_json("same", "/bin/demo", true));
  auto duplicate = ava::mcp::load_mcp_config(ava::mcp::McpConfigLoadOptions{
      .workspace_dir = root / "workspace", .global_config_file = global_path, .project_config_file = project_path});
  expect(!duplicate && duplicate.error().message().find("duplicate") != std::string::npos,
         "MCP config rejects duplicate server ids across scopes");
}

void test_mcp_stdio_client_lists_and_calls_tools() {
  const auto root = temp_root() / "mcp-client";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  std::filesystem::create_directories(workspace);

  auto startup_cancel_server = fake_server_config(root);
  startup_cancel_server.args = {"timeout-initialize"};
  int startup_cancel_checks = 0;
  auto startup_canceled = ava::mcp::McpStdioClient::start(startup_cancel_server, fake_client_options(workspace),
                                                          [&] { return ++startup_cancel_checks > 2; });
  expect(!startup_canceled && startup_canceled.error().message().find("canceled") != std::string::npos,
         "MCP stdio client cancels hung startup before timeout");

  auto client = ava::mcp::McpStdioClient::start(fake_server_config(root), fake_client_options(workspace));
  expect(client.has_value(), client ? "MCP stdio client initializes fake server"
                                    : "MCP stdio client initializes fake server: " + client.error().format());
  if (!client) return;

  expect((*client)->initialization().server_name == "fake-mcp", "MCP stdio client records server info");
  auto tools = (*client)->list_tools();
  expect(tools && tools->size() == 1 && tools->front().name == "echo" &&
             tools->front().input_schema_json.find("required") != std::string::npos,
         tools ? "MCP stdio client lists tools" : "MCP stdio client lists tools: " + tools.error().format());
  auto result = (*client)->call_tool("echo", "{\"text\":\"hello\"}");
  expect(result && !result->is_error && result->content == "MCP call ok",
         result ? "MCP stdio client calls tools" : "MCP stdio client calls tools: " + result.error().format());
  auto shutdown = (*client)->shutdown(std::chrono::milliseconds(500));
  expect(shutdown.has_value(), shutdown ? "MCP stdio client shuts down cleanly"
                                        : "MCP stdio client shuts down cleanly: " + shutdown.error().format());

  auto slow_server = fake_server_config(root);
  slow_server.args = {"slow-tool"};
  auto slow_client = ava::mcp::McpStdioClient::start(slow_server, fake_client_options(workspace));
  expect(slow_client.has_value(),
         slow_client ? "MCP stdio client initializes slow fake server"
                     : "MCP stdio client initializes slow fake server: " + slow_client.error().format());
  if (slow_client) {
    int cancel_checks = 0;
    auto canceled = (*slow_client)->call_tool("echo", "{\"text\":\"hello\"}", [&] { return ++cancel_checks > 2; });
    expect(!canceled && canceled.error().message().find("canceled") != std::string::npos,
           "MCP stdio client cancels hung tool calls before timeout");
    auto slow_shutdown = (*slow_client)->shutdown(std::chrono::milliseconds(500));
    expect(slow_shutdown.has_value(), "MCP stdio client shuts down after canceled tool call");
  }
}

void test_mcp_tool_dispatcher() {
  const auto root = temp_root() / "mcp-dispatcher";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  const auto project_config = workspace / ".ava" / "mcp.json";
  std::filesystem::create_directories(workspace);
  write_text(project_config, mcp_config_json("demo", AVA_FAKE_MCP_SERVER_PATH, true));

  std::vector<ava::permissions::PermissionPrompt> prompts;
  std::vector<ava::tools::PermissionAuditEvent> audits;
  bool cancel_requested = false;
  ava::tools::ToolContext context;
  context.workspace_dir = workspace;
  context.mcp_global_config_file = root / "missing-global-mcp.json";
  context.mcp_project_config_file = project_config;
  context.permission_resolver = [&prompts](const ava::permissions::PermissionPrompt& prompt)
      -> ava::core::Result<ava::permissions::PermissionResolution> {
    prompts.push_back(prompt);
    return ava::permissions::PermissionResolution::Allow;
  };
  context.permission_audit_sink = [&audits](const ava::tools::PermissionAuditEvent& event) -> ava::core::VoidResult {
    audits.push_back(event);
    return {};
  };
  context.cancel_requested = [&] { return cancel_requested; };

  const auto model_tool_name = ava::mcp::mcp_model_tool_name("demo", "echo");
  const auto schemas = ava::agent::ToolDispatcher::tool_schemas_json(context);
  const bool has_mcp_schema = std::any_of(schemas.begin(), schemas.end(), [&](const std::string& schema) {
    return schema.find("\"name\":\"" + model_tool_name + "\"") != std::string::npos &&
           schema.find("\"parameters\"") != std::string::npos;
  });
  expect(has_mcp_schema, "enabled MCP tool is exported as a provider schema");

  ava::agent::ToolDispatcher dispatcher(context);
  auto dispatched = dispatcher.dispatch(ava::agent::ProviderToolCall{
      .id = "call_mcp", .name = model_tool_name, .arguments_json = "{\"text\":\"hello\"}"});
  expect(dispatched && dispatched->success && dispatched->result_text.find("MCP call ok") != std::string::npos,
         dispatched ? "dispatcher calls enabled MCP tool through broker"
                    : "dispatcher calls enabled MCP tool through broker: " + dispatched.error().format());
  expect(dispatched && dispatched->payload.status == ava::agent::ToolResultStatus::Success &&
             dispatched->payload.content_type == "application/json" &&
             dispatched->payload.content.find("\"server\":\"demo\"") != std::string::npos &&
             dispatched->payload.content.find("MCP call ok") != std::string::npos,
         "MCP tool dispatcher attaches structured semantic result payloads");

  const auto has_launch = std::any_of(prompts.begin(), prompts.end(), [](const auto& prompt) {
    return prompt.operation == ava::permissions::Operation::McpServerLaunch;
  });
  const auto has_connect = std::any_of(prompts.begin(), prompts.end(), [](const auto& prompt) {
    return prompt.operation == ava::permissions::Operation::McpServerConnect;
  });
  const auto has_tool_call = std::any_of(prompts.begin(), prompts.end(), [&](const auto& prompt) {
    return prompt.operation == ava::permissions::Operation::McpToolCall && prompt.tool_name == model_tool_name &&
           prompt.command == "demo:echo";
  });
  expect(has_launch && has_connect && has_tool_call,
         "MCP tools require launch, connect, and mcp.tool.call permission approval");
  const auto audited_tool_call = std::any_of(audits.begin(), audits.end(), [](const auto& event) {
    return event.operation == ava::permissions::Operation::McpToolCall && event.resolution == "allow";
  });
  expect(audited_tool_call, "MCP tool permission decisions are audited");

  auto invalid_args = dispatcher.dispatch(
      ava::agent::ProviderToolCall{.id = "call_bad_args", .name = model_tool_name, .arguments_json = "[]"});
  expect(invalid_args && !invalid_args->success && invalid_args->result_text.find("JSON object") != std::string::npos,
         "MCP tool dispatcher rejects non-object arguments before execution");
  expect(invalid_args && invalid_args->payload.status == ava::agent::ToolResultStatus::Error &&
             invalid_args->payload.error_category == "invalid_argument",
         "MCP tool dispatcher attaches structured semantic error payloads");

  const auto prompts_before_cancel = prompts.size();
  cancel_requested = true;
  auto canceled = dispatcher.dispatch(
      ava::agent::ProviderToolCall{.id = "call_canceled", .name = model_tool_name, .arguments_json = "{}"});
  expect(canceled && !canceled->success && canceled->payload.status == ava::agent::ToolResultStatus::Canceled &&
             canceled->result_text.find("canceled") != std::string::npos && prompts.size() == prompts_before_cancel,
         "MCP tool dispatcher reports semantic cancellation before permission or process execution");
}

}  // namespace

void run_mcp_tests() {
  test_mcp_config_parsing();
  test_mcp_stdio_client_lists_and_calls_tools();
  test_mcp_tool_dispatcher();
}
