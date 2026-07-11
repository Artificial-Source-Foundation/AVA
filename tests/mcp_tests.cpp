#include "sys.h"
#include "tests/support/golden.h"
#include "tests/support/test_harness.h"
#include "ava/app/headless_policy.h"
#include "ava/agent/tool_dispatcher.h"
#include "ava/tools/file_tools.h"
#include "ava/mcp/config.h"
#include "ava/mcp/protocol.h"
#include "ava/mcp/stdio_client.h"
#include "ava/mcp/tool_broker.h"
#include "ava/permissions/permission.h"
#include "ava/core/json.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <signal.h>
#include <sys/types.h>

#ifndef AVA_FAKE_MCP_SERVER_PATH
#define AVA_FAKE_MCP_SERVER_PATH ""
#endif

namespace {

void write_text(std::filesystem::path const& path, std::string const& text)
{
  std::filesystem::create_directories(path.parent_path());
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file << text;
}

std::string read_text(std::filesystem::path const& path)
{
  std::ifstream file(path, std::ios::binary);
  std::ostringstream buffer;
  buffer << file.rdbuf();
  auto value = buffer.str();
  if (!value.empty() && value.back() == '\n')
    value.pop_back();
  return value;
}

std::optional<pid_t> read_pid_file_for_test(std::filesystem::path const& path)
{
  std::ifstream file(path, std::ios::binary);
  long long value = 0;
  file >> value;
  if (!file || value <= 0)
    return std::nullopt;
  return static_cast<pid_t>(value);
}

bool process_group_exists(pid_t pgid)
{
  errno = 0;
  if (::kill(-pgid, 0) == 0)
    return true;
  return errno != ESRCH;
}

bool wait_for_process_group_exit(pid_t pgid)
{
  for (int index = 0; index < 100; ++index)
  {
    if (!process_group_exists(pgid))
      return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return !process_group_exists(pgid);
}

std::string mcp_config_json(std::string id, std::string command, bool enabled = true, std::string args_json = "[]")
{
  return std::string("{\"servers\":[{\"id\":\"") + ava::core::json::escape(id) + "\",\"name\":\"Demo MCP\",\"command\":\"" + ava::core::json::escape(command) +
         "\",\"args\":" + args_json + ",\"enabled\":" + (enabled ? "true" : "false") + "}]}";
}

ava::mcp::McpServerConfig fake_server_config(std::filesystem::path const& root)
{
  return ava::mcp::McpServerConfig{.id = "demo",
                                   .name = "Demo MCP",
                                   .command = AVA_FAKE_MCP_SERVER_PATH,
                                   .args = {},
                                   .enabled = true,
                                   .scope = ava::mcp::McpServerScope::Project,
                                   .source_path = root / "mcp.json"};
}

ava::mcp::McpStdioClientOptions fake_client_options(std::filesystem::path const& workspace)
{
  ava::mcp::McpStdioClientOptions options;
  options.workspace_dir = workspace;
  options.startup_timeout = std::chrono::milliseconds(500);
  options.request_timeout = std::chrono::milliseconds(500);
  return options;
}

std::string nested_arrays_json(std::size_t depth)
{
  std::string text;
  text.reserve(depth * 2);
  for (std::size_t index = 0; index < depth; ++index) text += '[';
  for (std::size_t index = 0; index < depth; ++index) text += ']';
  return text;
}

ava::permissions::PermissionPrompt const* prompt_for_operation(std::vector<ava::permissions::PermissionPrompt> const& prompts,
                                                               ava::permissions::Operation operation)
{
  auto const found = std::find_if(prompts.begin(), prompts.end(), [&](auto const& prompt) { return prompt.operation == operation; });
  return found == prompts.end() ? nullptr : &*found;
}

bool has_audit_for_prompt(std::vector<ava::tools::PermissionAuditEvent> const& audits, ava::permissions::PermissionPrompt const& prompt,
                          std::string const& resolution, std::string const& resolution_source)
{
  return std::any_of(audits.begin(), audits.end(), [&](auto const& event) {
    return event.permission_request_id == prompt.permission_request_id && event.operation == prompt.operation && event.resolution == resolution &&
           event.resolution_source == resolution_source;
  });
}

void test_mcp_config_parsing()
{
  auto global = ava::mcp::parse_mcp_config("{\"servers\":[{\"id\":\"demo\",\"command\":\"/bin/demo\",\"args\":[\"--stdio\"]}]}", "/tmp/global-mcp.json",
                                           ava::mcp::McpServerScope::Global);
  expect(global && global->servers.size() == 1 && global->servers[0].enabled && global->servers[0].args.size() == 1 &&
             global->servers[0].scope == ava::mcp::McpServerScope::Global,
         global ? "MCP config parses global server defaults" : "MCP config parses global server defaults: " + global.error().format());

  auto project = ava::mcp::parse_mcp_config("{\"servers\":[{\"id\":\"demo-project\",\"command\":\"/bin/demo\"}]}", "/tmp/project-mcp.json",
                                            ava::mcp::McpServerScope::Project);
  expect(project && project->servers.size() == 1 && !project->servers[0].enabled,
         project ? "MCP project config servers default to disabled" : "MCP project config servers default to disabled: " + project.error().format());

  auto bad_id = ava::mcp::parse_mcp_config("{\"servers\":[{\"id\":\"bad id\",\"command\":\"demo\"}]}", "/tmp/bad-mcp.json", ava::mcp::McpServerScope::Global);
  expect(!bad_id && bad_id.error().message().find("valid id") != std::string::npos, "MCP config rejects invalid server ids");

  auto global_relative = ava::mcp::parse_mcp_config("{\"servers\":[{\"id\":\"relative\",\"command\":\"node\",\"args\":[\".ava/mcp-server.js\"]}]}",
                                                    "/tmp/global-mcp.json", ava::mcp::McpServerScope::Global);
  expect(!global_relative && global_relative.error().message().find("workspace-relative") != std::string::npos,
         "MCP global config rejects workspace-relative command arguments before project trust can be bypassed");

  auto project_relative =
      ava::mcp::parse_mcp_config("{\"servers\":[{\"id\":\"relative\",\"command\":\"node\",\"args\":[\".ava/mcp-server.js\"],\"enabled\":true}]}",
                                 "/tmp/project-mcp.json", ava::mcp::McpServerScope::Project);
  expect(project_relative && project_relative->servers.size() == 1,
         project_relative ? "MCP project config may use workspace-relative commands after project trust"
                          : "MCP project config may use workspace-relative commands after project trust: " + project_relative.error().format());

  auto const root = temp_root() / "mcp-config";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const global_path = root / "global" / "mcp.json";
  auto const project_path = root / "workspace" / ".ava" / "mcp.json";
  write_text(global_path, mcp_config_json("same", "/bin/demo"));
  write_text(project_path, mcp_config_json("same", "/bin/demo", true));
  auto duplicate = ava::mcp::load_mcp_config(
      ava::mcp::McpConfigLoadOptions{.workspace_dir = root / "workspace", .global_config_file = global_path, .project_config_file = project_path});
  expect(!duplicate && duplicate.error().message().find("duplicate") != std::string::npos, "MCP config rejects duplicate server ids across scopes");
}

void test_mcp_protocol_parsing()
{
  auto const server = fake_server_config("/tmp/mcp-protocol");

  auto content_length = ava::mcp::parse_mcp_content_length("X-Test: ignored\r\n content-length : 42\r\n\r\n", server, 64);
  expect(content_length && *content_length == 42, content_length
                                                      ? "MCP protocol parses trimmed case-insensitive Content-Length"
                                                      : "MCP protocol parses trimmed case-insensitive Content-Length: " + content_length.error().format());

  auto invalid_length = ava::mcp::parse_mcp_content_length("Content-Length: 4x\r\n\r\n", server, 64);
  expect(!invalid_length && invalid_length.error().message().find("invalid") != std::string::npos, "MCP protocol rejects invalid Content-Length values");

  auto oversized_length = ava::mcp::parse_mcp_content_length("Content-Length: 65\r\n\r\n", server, 64);
  expect(!oversized_length && oversized_length.error().message().find("size cap") != std::string::npos, "MCP protocol rejects oversized Content-Length values");

  expect(ava::mcp::mcp_header_end_offset("Content-Length: 2\r\n\r\n{}").value_or(0) == 21 &&
             ava::mcp::mcp_header_end_offset("Content-Length: 2\n\n{}").value_or(0) == 19,
         "MCP protocol finds CRLF and LF header terminators");

  expect(ava::mcp::mcp_response_id("{\"jsonrpc\":\"2.0\",\"id\":\"abc\",\"result\":{}}").value_or("") == "abc" &&
             ava::mcp::mcp_response_id("{\"jsonrpc\":\"2.0\",\"id\":42,\"result\":{}}").value_or("") == "42",
         "MCP protocol parses string and numeric response IDs");

  expect(ava::mcp::mcp_bool_field("{\"isError\":false}", "isError").value_or(true) == false, "MCP protocol parses boolean result fields");

  expect(ava::mcp::is_valid_mcp_tool_name("server.tool") && !ava::mcp::is_valid_mcp_tool_name("") && !ava::mcp::is_valid_mcp_tool_name("bad\nname"),
         "MCP protocol validates model-facing tool names");
  expect(ava::mcp::is_valid_mcp_resource_uri("file:///workspace/notes.md") && !ava::mcp::is_valid_mcp_resource_uri("") &&
             !ava::mcp::is_valid_mcp_resource_uri("bad\nuri"),
         "MCP protocol validates resource URIs");

  auto text_content = ava::mcp::mcp_text_content_from_result(
      "{\"content\":[{\"type\":\"text\",\"text\":\"one\"},{\"type\":\"image\",\"data\":\"ignored\"},"
      "{\"type\":\"text\",\"text\":\"two\"}]}");
  expect(text_content == "one\ntwo", "MCP protocol joins text content blocks");

  auto structured_content = ava::mcp::mcp_text_content_from_result("{\"structuredContent\":{\"ok\":true}}");
  expect(structured_content == "{\"ok\":true}", "MCP protocol falls back to structuredContent JSON");

  expect(!ava::mcp::mcp_json_depth_within_limit("{\"deep\":" + nested_arrays_json(130) + "}"), "MCP protocol rejects excessively deep JSON records");

  auto resource_content = ava::mcp::mcp_resource_text_from_result(
      "{\"contents\":[{\"uri\":\"file:///a\",\"mimeType\":\"text/plain\",\"text\":\"one\"},"
      "{\"uri\":\"file:///b\",\"blob\":\"ignored\"},{\"uri\":\"file:///c\",\"text\":\"two\"}]}");
  expect(resource_content == "one\ntwo", "MCP protocol joins text resource contents");
}

void test_mcp_permission_audit_golden_shape()
{
  ava::tools::PermissionAuditEvent event;
  event.permission_request_id = "permreq_ava080";
  event.operation = ava::permissions::Operation::McpToolCall;
  event.mode = ava::agent::Mode::Build;
  event.tool_name = "mcp_demo_echo";
  event.action = ava::permissions::PermissionAction::Ask;
  event.reason = "MCP tool calls require explicit approval";
  event.risk = ava::permissions::PermissionRisk::High;
  event.target_path = "/workspace/.ava/mcp.json";
  event.command = "demo:echo";
  event.resolution = "deny";
  event.resolution_source = "resolver";
  event.resolution_reason = "denied by contract test";

  ava::test::expect_json_matches_golden(ava::tools::permission_audit_data_json(event), "permission-audit.json",
                                        "permission audit JSON shape matches AVA 0.80 golden fixture");
}

void test_mcp_stdio_client_lists_and_calls_tools()
{
  auto const root = temp_root() / "mcp-client";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);

  auto const startup_cancel_pgid_file = root / "mcp-startup-cancel-pgid.txt";
  auto startup_cancel_server = fake_server_config(root);
  startup_cancel_server.args = {"timeout-initialize-marker", startup_cancel_pgid_file.generic_string()};
  auto startup_canceled = ava::mcp::McpStdioClient::start(startup_cancel_server, fake_client_options(workspace),
                                                          [&] { return read_pid_file_for_test(startup_cancel_pgid_file).has_value(); });
  auto const startup_cancel_pgid = read_pid_file_for_test(startup_cancel_pgid_file);
  expect(!startup_canceled && startup_canceled.error().message().find("canceled") != std::string::npos && startup_cancel_pgid &&
             wait_for_process_group_exit(*startup_cancel_pgid),
         "MCP stdio client cancels hung startup and terminates the server process group before timeout");

  auto malformed_server = fake_server_config(root);
  malformed_server.args = {"malformed"};
  auto malformed = ava::mcp::McpStdioClient::start(malformed_server, fake_client_options(workspace));
  expect(!malformed && malformed.error().message().find("valid JSON object") != std::string::npos, "MCP stdio client contains malformed initialize responses");

  auto exit_start_server = fake_server_config(root);
  exit_start_server.args = {"exit-initialize"};
  auto exit_start = ava::mcp::McpStdioClient::start(exit_start_server, fake_client_options(workspace));
  auto const exit_start_format = exit_start ? std::string{} : exit_start.error().format();
  expect(!exit_start && exit_start.error().message().find("closed stdout") != std::string::npos &&
             exit_start_format.find("status: exit 42") != std::string::npos && exit_start_format.find("fake MCP exited during initialize") != std::string::npos,
         "MCP stdio client reports early startup exit with status and stderr diagnostics: " + exit_start_format);

  auto const global_config_dir = root / "global-config";
  std::filesystem::create_directories(global_config_dir);
  auto const global_cwd_file = root / "mcp-global-cwd.txt";
  auto global_server = fake_server_config(root);
  global_server.scope = ava::mcp::McpServerScope::Global;
  global_server.source_path = global_config_dir / "mcp.json";
  global_server.args = {"cwd-marker", global_cwd_file.generic_string()};
  auto global_cwd_client = ava::mcp::McpStdioClient::start(global_server, fake_client_options(workspace));
  expect(global_cwd_client.has_value(), global_cwd_client ? "MCP global server initializes from safe config cwd"
                                                          : "MCP global server initializes from safe config cwd: " + global_cwd_client.error().format());
  if (global_cwd_client)
  {
    expect(read_text(global_cwd_file) == std::filesystem::weakly_canonical(global_config_dir).string(),
           "MCP global server process cwd is the global config directory, not the workspace");
    auto global_shutdown = (*global_cwd_client)->shutdown(std::chrono::milliseconds(500));
    expect(global_shutdown.has_value(), "MCP global cwd test server shuts down cleanly");
  }

  auto client = ava::mcp::McpStdioClient::start(fake_server_config(root), fake_client_options(workspace));
  expect(client.has_value(), client ? "MCP stdio client initializes fake server" : "MCP stdio client initializes fake server: " + client.error().format());
  if (!client)
    return;

  expect((*client)->initialization().server_name == "fake-mcp", "MCP stdio client records server info");
  auto tools = (*client)->list_tools();
  expect(tools && tools->size() == 1 && tools->front().name == "echo" && tools->front().input_schema_json.find("required") != std::string::npos,
         tools ? "MCP stdio client lists tools" : "MCP stdio client lists tools: " + tools.error().format());
  auto pre_canceled = (*client)->call_tool("echo", "{\"text\":\"hello\"}", [] { return true; });
  expect(!pre_canceled && pre_canceled.error().message().find("canceled") != std::string::npos,
         "MCP stdio client maps cancellation before tools/call to a deterministic canceled error");
  auto result = (*client)->call_tool("echo", "{\"text\":\"hello\"}");
  expect(result && !result->is_error && result->content == "MCP call ok",
         result ? "MCP stdio client calls tools" : "MCP stdio client calls tools: " + result.error().format());
  auto resources = (*client)->list_resources();
  expect(resources && resources->size() == 1 && resources->front().uri == "file:///workspace/notes.md" && resources->front().mime_type == "text/markdown",
         resources ? "MCP stdio client lists resources" : "MCP stdio client lists resources: " + resources.error().format());
  auto resource = (*client)->read_resource("file:///workspace/notes.md");
  expect(resource && resource->content == "MCP resource content" && resource->uri == "file:///workspace/notes.md" && resource->mime_type == "text/markdown",
         resource ? "MCP stdio client reads resources" : "MCP stdio client reads resources: " + resource.error().format());
  auto shutdown = (*client)->shutdown(std::chrono::milliseconds(500));
  expect(shutdown.has_value(), shutdown ? "MCP stdio client shuts down cleanly" : "MCP stdio client shuts down cleanly: " + shutdown.error().format());

  auto paginated_server = fake_server_config(root);
  paginated_server.args = {"paginated-resources"};
  auto paginated_client = ava::mcp::McpStdioClient::start(paginated_server, fake_client_options(workspace));
  expect(paginated_client.has_value(), paginated_client ? "MCP stdio client initializes paginated resource fake server"
                                                        : "MCP stdio client initializes paginated resource fake server: " + paginated_client.error().format());
  if (paginated_client)
  {
    auto paginated_resources = (*paginated_client)->list_resources();
    expect(paginated_resources && paginated_resources->size() == 2 && paginated_resources->at(1).uri == "file:///workspace/two.md",
           paginated_resources ? "MCP stdio client follows bounded resource list pagination"
                               : "MCP stdio client follows bounded resource list pagination: " + paginated_resources.error().format());
    auto paginated_shutdown = (*paginated_client)->shutdown(std::chrono::milliseconds(500));
    expect(paginated_shutdown.has_value(), "MCP stdio client shuts down after paginated resources/list");
  }

  auto blob_server = fake_server_config(root);
  blob_server.args = {"resource-blob"};
  auto blob_client = ava::mcp::McpStdioClient::start(blob_server, fake_client_options(workspace));
  expect(blob_client.has_value(), blob_client ? "MCP stdio client initializes blob resource fake server"
                                              : "MCP stdio client initializes blob resource fake server: " + blob_client.error().format());
  if (blob_client)
  {
    auto blob = (*blob_client)->read_resource("file:///workspace/blob.bin");
    expect(!blob && blob.error().message().find("no text content") != std::string::npos,
           "MCP stdio client rejects blob-only resource reads as unsupported text content");
    auto blob_shutdown = (*blob_client)->shutdown(std::chrono::milliseconds(500));
    expect(blob_shutdown.has_value(), "MCP stdio client shuts down after blob resource rejection");
  }

  auto missing_server = fake_server_config(root);
  missing_server.args = {"resource-missing-contents"};
  auto missing_client = ava::mcp::McpStdioClient::start(missing_server, fake_client_options(workspace));
  expect(missing_client.has_value(), missing_client ? "MCP stdio client initializes malformed resource fake server"
                                                    : "MCP stdio client initializes malformed resource fake server: " + missing_client.error().format());
  if (missing_client)
  {
    auto missing = (*missing_client)->read_resource("file:///workspace/missing.md");
    expect(!missing && missing.error().message().find("contents field") != std::string::npos,
           "MCP stdio client rejects resource reads without contents arrays");
    auto missing_shutdown = (*missing_client)->shutdown(std::chrono::milliseconds(500));
    expect(missing_shutdown.has_value(), "MCP stdio client shuts down after malformed resource rejection");
  }

  auto tool_error_server = fake_server_config(root);
  tool_error_server.args = {"tool-error"};
  auto tool_error_client = ava::mcp::McpStdioClient::start(tool_error_server, fake_client_options(workspace));
  expect(tool_error_client.has_value(), tool_error_client ? "MCP stdio client initializes tool-error fake server"
                                                          : "MCP stdio client initializes tool-error fake server: " + tool_error_client.error().format());
  if (tool_error_client)
  {
    auto tool_error = (*tool_error_client)->call_tool("echo", "{\"text\":\"hello\"}");
    expect(tool_error && tool_error->is_error && tool_error->content == "MCP tool failed",
           tool_error ? "MCP stdio client preserves tool-level error results"
                      : "MCP stdio client preserves tool-level error results: " + tool_error.error().format());
    auto tool_error_shutdown = (*tool_error_client)->shutdown(std::chrono::milliseconds(500));
    expect(tool_error_shutdown.has_value(), "MCP stdio client shuts down after tool-level error result");
  }

  auto error_call_server = fake_server_config(root);
  error_call_server.args = {"error-call"};
  auto error_call_client = ava::mcp::McpStdioClient::start(error_call_server, fake_client_options(workspace));
  expect(error_call_client.has_value(), error_call_client ? "MCP stdio client initializes JSON-RPC error fake server"
                                                          : "MCP stdio client initializes JSON-RPC error fake server: " + error_call_client.error().format());
  if (error_call_client)
  {
    auto error_call = (*error_call_client)->call_tool("echo", "{\"text\":\"hello\"}");
    expect(!error_call && error_call.error().message().find("fake MCP JSON-RPC call failed") != std::string::npos,
           "MCP stdio client surfaces JSON-RPC tool-call errors");
    auto error_call_shutdown = (*error_call_client)->shutdown(std::chrono::milliseconds(500));
    expect(error_call_shutdown.has_value(), "MCP stdio client shuts down after JSON-RPC error result");
  }

  auto exit_list_server = fake_server_config(root);
  exit_list_server.args = {"exit-after-initialize"};
  auto exit_list_client = ava::mcp::McpStdioClient::start(exit_list_server, fake_client_options(workspace));
  expect(exit_list_client.has_value(), exit_list_client
                                           ? "MCP stdio client initializes exit-after-initialize fake server"
                                           : "MCP stdio client initializes exit-after-initialize fake server: " + exit_list_client.error().format());
  if (exit_list_client)
  {
    auto exited_tools = (*exit_list_client)->list_tools();
    expect(!exited_tools && exited_tools.error().message().find("closed stdout") != std::string::npos &&
               exited_tools.error().format().find("status: exit 43") != std::string::npos &&
               exited_tools.error().format().find("fake MCP exited during tools/list") != std::string::npos,
           "MCP stdio client reports discovery-time process exit with status and stderr diagnostics");
    auto exit_list_shutdown = (*exit_list_client)->shutdown(std::chrono::milliseconds(500));
    expect(exit_list_shutdown.has_value(), "MCP stdio client shuts down after discovery-time exit");
  }

  auto stderr_server = fake_server_config(root);
  stderr_server.args = {"stderr-noise"};
  auto stderr_options = fake_client_options(workspace);
  stderr_options.max_stderr_bytes = 16;
  auto stderr_client = ava::mcp::McpStdioClient::start(stderr_server, stderr_options);
  expect(stderr_client.has_value(), stderr_client ? "MCP stdio client initializes noisy fake server"
                                                  : "MCP stdio client initializes noisy fake server: " + stderr_client.error().format());
  if (stderr_client)
  {
    auto stderr_tools = (*stderr_client)->list_tools();
    expect(stderr_tools.has_value(), "MCP stdio client drains stderr while listing tools");
    auto stderr_shutdown = (*stderr_client)->shutdown(std::chrono::milliseconds(500));
    expect(stderr_shutdown.has_value(), "MCP stdio client shuts down noisy fake server");
    expect((*stderr_client)->stderr_truncated(), "MCP stdio client bounds stderr diagnostics");
    expect((*stderr_client)->stderr_tail() == "mcp-stderr-tail!", "MCP stdio client keeps stderr tail");
  }

  auto const slow_tool_pgid_file = root / "mcp-tool-cancel-pgid.txt";
  auto slow_server = fake_server_config(root);
  slow_server.args = {"slow-tool-marker", slow_tool_pgid_file.generic_string()};
  auto slow_client = ava::mcp::McpStdioClient::start(slow_server, fake_client_options(workspace));
  expect(slow_client.has_value(),
         slow_client ? "MCP stdio client initializes slow fake server" : "MCP stdio client initializes slow fake server: " + slow_client.error().format());
  if (slow_client)
  {
    auto canceled = (*slow_client)->call_tool("echo", "{\"text\":\"hello\"}", [&] { return read_pid_file_for_test(slow_tool_pgid_file).has_value(); });
    auto const slow_tool_pgid = read_pid_file_for_test(slow_tool_pgid_file);
    expect(!canceled && canceled.error().message().find("canceled") != std::string::npos && slow_tool_pgid && wait_for_process_group_exit(*slow_tool_pgid),
           "MCP stdio client cancels hung tool calls and terminates the server process group before timeout");
    auto slow_shutdown = (*slow_client)->shutdown(std::chrono::milliseconds(500));
    expect(slow_shutdown.has_value(), "MCP stdio client shuts down after canceled tool call");
  }

  auto exit_tool_server = fake_server_config(root);
  exit_tool_server.args = {"exit-tool"};
  auto exit_tool_client = ava::mcp::McpStdioClient::start(exit_tool_server, fake_client_options(workspace));
  expect(exit_tool_client.has_value(), exit_tool_client ? "MCP stdio client initializes exit-tool fake server"
                                                        : "MCP stdio client initializes exit-tool fake server: " + exit_tool_client.error().format());
  if (exit_tool_client)
  {
    auto exited_tool = (*exit_tool_client)->call_tool("echo", "{\"text\":\"hello\"}");
    auto const exited_tool_format = exited_tool ? std::string{} : exited_tool.error().format();
    expect(!exited_tool && exited_tool.error().message().find("closed stdout") != std::string::npos &&
               exited_tool_format.find("status: exit 44") != std::string::npos &&
               exited_tool_format.find("fake MCP exited during tools/call") != std::string::npos,
           "MCP stdio client reports tool-call process exit with status and stderr diagnostics: " + exited_tool_format);
    auto exit_tool_shutdown = (*exit_tool_client)->shutdown(std::chrono::milliseconds(500));
    expect(exit_tool_shutdown.has_value(), "MCP stdio client shuts down after tool-call process exit");
  }

  auto const slow_prompt_pgid_file = root / "mcp-prompt-cancel-pgid.txt";
  auto slow_prompt_server = fake_server_config(root);
  slow_prompt_server.args = {"slow-prompt-marker", slow_prompt_pgid_file.generic_string()};
  auto slow_prompt_client = ava::mcp::McpStdioClient::start(slow_prompt_server, fake_client_options(workspace));
  expect(slow_prompt_client.has_value(), slow_prompt_client ? "MCP stdio client initializes slow-prompt fake server"
                                                            : "MCP stdio client initializes slow-prompt fake server: " + slow_prompt_client.error().format());
  if (slow_prompt_client)
  {
    auto canceled_prompt =
        (*slow_prompt_client)->get_prompt("release-notes", "{\"topic\":\"demo\"}", [&] { return read_pid_file_for_test(slow_prompt_pgid_file).has_value(); });
    auto const slow_prompt_pgid = read_pid_file_for_test(slow_prompt_pgid_file);
    expect(!canceled_prompt && canceled_prompt.error().message().find("canceled") != std::string::npos && slow_prompt_pgid &&
               wait_for_process_group_exit(*slow_prompt_pgid),
           "MCP stdio client cancels hung prompts/get calls and terminates the server process group before timeout");
    auto slow_prompt_shutdown = (*slow_prompt_client)->shutdown(std::chrono::milliseconds(500));
    expect(slow_prompt_shutdown.has_value(), "MCP stdio client shuts down after canceled prompt get");
  }

  auto const slow_resource_pgid_file = root / "mcp-resource-cancel-pgid.txt";
  auto slow_resource_server = fake_server_config(root);
  slow_resource_server.args = {"slow-resource-marker", slow_resource_pgid_file.generic_string()};
  auto slow_resource_client = ava::mcp::McpStdioClient::start(slow_resource_server, fake_client_options(workspace));
  expect(slow_resource_client.has_value(), slow_resource_client
                                               ? "MCP stdio client initializes slow-resource fake server"
                                               : "MCP stdio client initializes slow-resource fake server: " + slow_resource_client.error().format());
  if (slow_resource_client)
  {
    auto canceled_resource =
        (*slow_resource_client)->read_resource("file:///workspace/notes.md", [&] { return read_pid_file_for_test(slow_resource_pgid_file).has_value(); });
    auto const slow_resource_pgid = read_pid_file_for_test(slow_resource_pgid_file);
    expect(!canceled_resource && canceled_resource.error().message().find("canceled") != std::string::npos && slow_resource_pgid &&
               wait_for_process_group_exit(*slow_resource_pgid),
           "MCP stdio client cancels hung resources/read calls and terminates the server process group before timeout");
    auto slow_resource_shutdown = (*slow_resource_client)->shutdown(std::chrono::milliseconds(500));
    expect(slow_resource_shutdown.has_value(), "MCP stdio client shuts down after canceled resource read");
  }
}

void test_mcp_tool_dispatcher()
{
  auto const root = temp_root() / "mcp-dispatcher";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const project_config = workspace / ".ava" / "mcp.json";
  std::filesystem::create_directories(workspace);
  write_text(project_config, mcp_config_json("demo", AVA_FAKE_MCP_SERVER_PATH, true));

  std::vector<ava::permissions::PermissionPrompt> prompts;
  std::vector<ava::tools::PermissionAuditEvent> audits;
  bool cancel_requested = false;
  ava::tools::ToolContext context;
  context.workspace_dir = workspace;
  context.mcp_global_config_file = root / "missing-global-mcp.json";
  context.mcp_project_config_file = project_config;
  context.permission_resolver =
      [&prompts](ava::permissions::PermissionPrompt const& prompt) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
    prompts.push_back(prompt);
    return ava::permissions::PermissionResolution::Allow;
  };
  context.permission_audit_sink = [&audits](ava::tools::PermissionAuditEvent const& event) -> ava::core::VoidResult {
    audits.push_back(event);
    return {};
  };
  context.cancel_requested = [&] { return cancel_requested; };

  auto const model_tool_name = ava::mcp::mcp_model_tool_name("demo", "echo");
  auto const resource_tool_name = ava::mcp::mcp_model_resource_tool_name("demo", "file:///workspace/notes.md");
  auto const schemas = ava::agent::ToolDispatcher::tool_schemas_json(context);
  auto const mcp_schema = std::find_if(schemas.begin(), schemas.end(), [&](std::string const& schema) {
    return schema.find("\"name\":\"" + model_tool_name + "\"") != std::string::npos && schema.find("\"parameters\"") != std::string::npos;
  });
  expect(mcp_schema != schemas.end(), "enabled MCP tool is exported as a provider schema");
  if (mcp_schema != schemas.end())
  {
    ava::test::expect_json_matches_golden(*mcp_schema, "mcp-tool-schema.json", "MCP tool provider schema matches AVA 0.80 golden fixture");
  }
  auto const resource_schema = std::find_if(schemas.begin(), schemas.end(), [&](std::string const& schema) {
    return schema.find("\"name\":\"" + resource_tool_name + "\"") != std::string::npos && schema.find("\"additionalProperties\":false") != std::string::npos &&
           schema.find("file:///workspace/notes.md") == std::string::npos && schema.find("Project notes resource") == std::string::npos;
  });
  expect(resource_schema != schemas.end(), "enabled MCP resource is exported as an opaque no-argument provider schema");

  ava::agent::ToolDispatcher dispatcher(context);
  auto dispatched = dispatcher.dispatch(ava::agent::ProviderToolCall{.id = "call_mcp", .name = model_tool_name, .arguments_json = "{\"text\":\"hello\"}"});
  expect(dispatched && dispatched->success && dispatched->result_text.find("MCP call ok") != std::string::npos,
         dispatched ? "dispatcher calls enabled MCP tool through broker" : "dispatcher calls enabled MCP tool through broker: " + dispatched.error().format());
  expect(dispatched && dispatched->payload.status == ava::agent::ToolResultStatus::Success && dispatched->payload.content_type == "application/json" &&
             dispatched->payload.content.find("\"server\":\"demo\"") != std::string::npos &&
             dispatched->payload.content.find("MCP call ok") != std::string::npos,
         "MCP tool dispatcher attaches structured semantic result payloads");

  auto const has_launch =
      std::any_of(prompts.begin(), prompts.end(), [](auto const& prompt) { return prompt.operation == ava::permissions::Operation::McpServerLaunch; });
  auto const has_connect =
      std::any_of(prompts.begin(), prompts.end(), [](auto const& prompt) { return prompt.operation == ava::permissions::Operation::McpServerConnect; });
  auto const has_tool_call = std::any_of(prompts.begin(), prompts.end(), [&](auto const& prompt) {
    return prompt.operation == ava::permissions::Operation::McpToolCall && prompt.tool_name == model_tool_name && prompt.command == "demo:echo";
  });
  expect(has_launch && has_connect && has_tool_call, "MCP tools require launch, connect, and mcp.tool.call permission approval");
  auto const audited_tool_call = std::any_of(audits.begin(), audits.end(), [](auto const& event) {
    return event.operation == ava::permissions::Operation::McpToolCall && event.resolution == "allow";
  });
  expect(audited_tool_call, "MCP tool permission decisions are audited");

  auto resource_dispatched = dispatcher.dispatch(ava::agent::ProviderToolCall{.id = "call_mcp_resource", .name = resource_tool_name, .arguments_json = "{}"});
  expect(resource_dispatched && resource_dispatched->success && resource_dispatched->result_text.find("MCP resource content") != std::string::npos,
         resource_dispatched ? "dispatcher reads enabled MCP resources through broker"
                             : "dispatcher reads enabled MCP resources through broker: " + resource_dispatched.error().format());
  expect(resource_dispatched && resource_dispatched->payload.status == ava::agent::ToolResultStatus::Success &&
             resource_dispatched->payload.content_type == "application/json" &&
             resource_dispatched->payload.content.find("\"mcp_resource\":\"file:///workspace/notes.md\"") != std::string::npos,
         "MCP resource dispatcher attaches structured semantic result payloads");
  auto const has_resource_read = std::any_of(prompts.begin(), prompts.end(), [&](auto const& prompt) {
    return prompt.operation == ava::permissions::Operation::McpResourceRead && prompt.tool_name == resource_tool_name &&
           prompt.command == "demo:file:///workspace/notes.md";
  });
  expect(has_resource_read, "MCP resources require mcp.resource.read permission approval");
  auto const audited_resource_read = std::any_of(audits.begin(), audits.end(), [](auto const& event) {
    return event.operation == ava::permissions::Operation::McpResourceRead && event.resolution == "allow";
  });
  expect(audited_resource_read, "MCP resource permission decisions are audited");

  auto resource_bad_args = dispatcher.dispatch(
      ava::agent::ProviderToolCall{.id = "call_mcp_resource_bad_args", .name = resource_tool_name, .arguments_json = "{\"uri\":\"other\"}"});
  expect(resource_bad_args && !resource_bad_args->success && resource_bad_args->result_text.find("empty JSON object") != std::string::npos,
         "MCP resource dispatcher rejects caller-supplied arguments before execution");
  expect(resource_bad_args && resource_bad_args->result_text.find("file:///workspace/notes.md") == std::string::npos,
         "MCP resource invalid-argument errors do not leak raw resource URIs before read approval");

  auto invalid_args = dispatcher.dispatch(ava::agent::ProviderToolCall{.id = "call_bad_args", .name = model_tool_name, .arguments_json = "[]"});
  expect(invalid_args && !invalid_args->success && invalid_args->result_text.find("JSON object") != std::string::npos,
         "MCP tool dispatcher rejects non-object arguments before execution");
  expect(invalid_args && invalid_args->payload.status == ava::agent::ToolResultStatus::Error && invalid_args->payload.error_category == "invalid_argument",
         "MCP tool dispatcher attaches structured semantic error payloads");

  auto const prompts_before_cancel = prompts.size();
  cancel_requested = true;
  auto canceled = dispatcher.dispatch(ava::agent::ProviderToolCall{.id = "call_canceled", .name = model_tool_name, .arguments_json = "{}"});
  expect(canceled && !canceled->success && canceled->payload.status == ava::agent::ToolResultStatus::Canceled &&
             canceled->result_text.find("canceled") != std::string::npos && prompts.size() == prompts_before_cancel,
         "MCP tool dispatcher reports semantic cancellation before permission or process execution");
}

void test_mcp_tool_dispatcher_audits_permission_denials()
{
  auto run_denial_case = [](std::string const& suffix, ava::permissions::Operation denied_operation, std::string const& expected_error) {
    auto const root = temp_root() / ("mcp-denial-" + suffix);
    std::error_code remove_error;
    std::filesystem::remove_all(root, remove_error);
    auto const workspace = root / "workspace";
    auto const project_config = workspace / ".ava" / "mcp.json";
    std::filesystem::create_directories(workspace);
    write_text(project_config, mcp_config_json("demo", AVA_FAKE_MCP_SERVER_PATH, true));

    std::vector<ava::permissions::PermissionPrompt> prompts;
    std::vector<ava::tools::PermissionAuditEvent> audits;
    std::optional<ava::permissions::Operation> operation_to_deny;
    ava::tools::ToolContext context;
    context.workspace_dir = workspace;
    context.mcp_global_config_file = root / "missing-global-mcp.json";
    context.mcp_project_config_file = project_config;
    context.permission_resolver = [&](ava::permissions::PermissionPrompt const& prompt) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
      prompts.push_back(prompt);
      if (operation_to_deny && prompt.operation == *operation_to_deny)
      {
        return ava::permissions::PermissionResolutionDecision(ava::permissions::PermissionResolution::Deny, "denied by contract test");
      }
      return ava::permissions::PermissionResolutionDecision(ava::permissions::PermissionResolution::Allow, "allowed by contract test setup");
    };
    context.permission_audit_sink = [&audits](ava::tools::PermissionAuditEvent const& event) -> ava::core::VoidResult {
      audits.push_back(event);
      return {};
    };

    auto const model_tool_name = denied_operation == ava::permissions::Operation::McpResourceRead
                                     ? ava::mcp::mcp_model_resource_tool_name("demo", "file:///workspace/notes.md")
                                     : ava::mcp::mcp_model_tool_name("demo", "echo");
    auto const arguments_json = denied_operation == ava::permissions::Operation::McpResourceRead ? "{}" : "{\"text\":\"hello\"}";
    ava::agent::ToolDispatcher dispatcher(context);
    prompts.clear();
    audits.clear();
    operation_to_deny = denied_operation;

    auto dispatched =
        dispatcher.dispatch(ava::agent::ProviderToolCall{.id = "call_mcp_denied_" + suffix, .name = model_tool_name, .arguments_json = arguments_json});
    expect(dispatched && !dispatched->success && dispatched->result_text.find(expected_error) != std::string::npos,
           dispatched ? "MCP dispatcher reports permission denial for " + suffix
                      : "MCP dispatcher reports permission denial for " + suffix + ": " + dispatched.error().format());
    if (denied_operation == ava::permissions::Operation::McpResourceRead)
    {
      expect(dispatched && dispatched->result_text.find("file:///workspace/notes.md") == std::string::npos,
             "MCP resource denial result does not leak raw resource URI before approval");
    }

    auto const* denied_prompt = prompt_for_operation(prompts, denied_operation);
    expect(denied_prompt != nullptr && !denied_prompt->permission_request_id.empty(), "MCP denial prompt carries a request id for " + suffix);
    if (denied_prompt != nullptr)
    {
      auto const linked_audits =
          std::count_if(audits.begin(), audits.end(), [&](auto const& event) { return event.permission_request_id == denied_prompt->permission_request_id; });
      expect(linked_audits >= 2, "MCP denial records policy and resolver audit events with the prompt request id for " + suffix);
      expect(has_audit_for_prompt(audits, *denied_prompt, "deny", "resolver"), "MCP denial audit records resolver denial for " + suffix);
      if (dispatched)
      {
        expect(std::find(dispatched->payload.permission_request_ids.begin(), dispatched->payload.permission_request_ids.end(),
                         denied_prompt->permission_request_id) != dispatched->payload.permission_request_ids.end(),
               "MCP denial result payload links the permission request id for " + suffix);
      }
    }
  };

  run_denial_case("launch", ava::permissions::Operation::McpServerLaunch, "MCP server launch requires permission");
  run_denial_case("connect", ava::permissions::Operation::McpServerConnect, "MCP server connection requires permission");
  run_denial_case("tool", ava::permissions::Operation::McpToolCall, "MCP tool call requires permission");
  run_denial_case("resource", ava::permissions::Operation::McpResourceRead, "MCP resource read requires permission");
}

void test_mcp_headless_policy_allows_resource_reads()
{
  ava::app::HeadlessPermissionPolicyOptions options;
  auto added = ava::app::add_headless_allowed_tools(options, "mcp");
  expect(added.has_value(), "headless policy accepts mcp allow-tool value");
  auto resolver = ava::app::build_headless_permission_resolver(std::move(options));

  ava::permissions::PermissionPrompt prompt;
  prompt.operation = ava::permissions::Operation::McpResourceRead;
  prompt.tool_name = ava::mcp::mcp_model_resource_tool_name("demo", "file:///workspace/notes.md");
  prompt.command = "demo:file:///workspace/notes.md";
  auto decision = resolver(prompt);
  expect(
      decision && decision->resolution == ava::permissions::PermissionResolution::Allow,
      decision ? "headless --allow-tool mcp permits MCP resource reads" : "headless --allow-tool mcp permits MCP resource reads: " + decision.error().format());
}

void test_mcp_tool_dispatcher_contains_tool_errors()
{
  auto const root = temp_root() / "mcp-dispatcher-tool-error";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const project_config = workspace / ".ava" / "mcp.json";
  std::filesystem::create_directories(workspace);
  write_text(project_config, mcp_config_json("demo", AVA_FAKE_MCP_SERVER_PATH, true, "[\"tool-error\"]"));

  std::vector<ava::permissions::PermissionPrompt> prompts;
  ava::tools::ToolContext context;
  context.workspace_dir = workspace;
  context.mcp_global_config_file = root / "missing-global-mcp.json";
  context.mcp_project_config_file = project_config;
  context.permission_resolver =
      [&prompts](ava::permissions::PermissionPrompt const& prompt) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
    prompts.push_back(prompt);
    return ava::permissions::PermissionResolution::Allow;
  };

  auto const model_tool_name = ava::mcp::mcp_model_tool_name("demo", "echo");
  ava::agent::ToolDispatcher dispatcher(context);
  auto dispatched =
      dispatcher.dispatch(ava::agent::ProviderToolCall{.id = "call_mcp_tool_error", .name = model_tool_name, .arguments_json = "{\"text\":\"hello\"}"});
  expect(dispatched && !dispatched->success && dispatched->payload.status == ava::agent::ToolResultStatus::Error &&
             dispatched->payload.content_type == "application/json" && dispatched->payload.content.find("\"ok\":false") != std::string::npos &&
             dispatched->payload.content.find("MCP tool failed") != std::string::npos,
         dispatched ? "MCP tool dispatcher contains tool-level error results as semantic payloads"
                    : "MCP tool dispatcher contains tool-level error results as semantic payloads: " + dispatched.error().format());
  expect(!prompts.empty(), "MCP tool dispatcher still gates tool-level error calls through permission prompts");

  auto const text_root = temp_root() / "mcp-dispatcher-canceled-text-error";
  std::filesystem::remove_all(text_root, remove_error);
  auto const text_workspace = text_root / "workspace";
  auto const text_project_config = text_workspace / ".ava" / "mcp.json";
  std::filesystem::create_directories(text_workspace);
  write_text(text_project_config, mcp_config_json("demo", AVA_FAKE_MCP_SERVER_PATH, true, "[\"tool-error-canceled-text\"]"));

  ava::tools::ToolContext text_context;
  text_context.workspace_dir = text_workspace;
  text_context.mcp_global_config_file = text_root / "missing-global-mcp.json";
  text_context.mcp_project_config_file = text_project_config;
  text_context.permission_resolver = [](ava::permissions::PermissionPrompt const&) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
    return ava::permissions::PermissionResolution::Allow;
  };
  ava::agent::ToolDispatcher text_dispatcher(text_context);
  auto text_dispatched = text_dispatcher.dispatch(
      ava::agent::ProviderToolCall{.id = "call_mcp_canceled_text_error", .name = model_tool_name, .arguments_json = "{\"text\":\"hello\"}"});
  expect(text_dispatched && !text_dispatched->success && text_dispatched->payload.status == ava::agent::ToolResultStatus::Error &&
             text_dispatched->payload.error_message.find("canceled upstream") != std::string::npos,
         text_dispatched
             ? "MCP tool dispatcher does not classify ordinary tool errors containing canceled as cancellation"
             : "MCP tool dispatcher does not classify ordinary tool errors containing canceled as cancellation: " + text_dispatched.error().format());
}

}  // namespace

void run_mcp_tests()
{
  test_mcp_config_parsing();
  test_mcp_protocol_parsing();
  test_mcp_permission_audit_golden_shape();
  test_mcp_stdio_client_lists_and_calls_tools();
  test_mcp_tool_dispatcher();
  test_mcp_tool_dispatcher_audits_permission_denials();
  test_mcp_tool_dispatcher_contains_tool_errors();
  test_mcp_headless_policy_allows_resource_reads();
}
