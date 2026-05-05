#include <chrono>
#include <filesystem>
#include <string>
#include <utility>

#include "ava/mcp/config.h"
#include "ava/mcp/stdio_client.h"
#include "tests/support/test_harness.h"

#ifndef AVA_FAKE_MCP_SERVER_PATH
#define AVA_FAKE_MCP_SERVER_PATH ""
#endif

namespace {

std::filesystem::path fresh_root(std::string const& name)
{
  auto const root = temp_root() / name;
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  return root;
}

ava::mcp::McpServerConfig fake_server_config(std::filesystem::path const& root, std::string mode = "")
{
  ava::mcp::McpServerConfig server{.id = "demo",
                                   .name = "Demo MCP",
                                   .command = AVA_FAKE_MCP_SERVER_PATH,
                                   .args = {},
                                   .enabled = true,
                                   .scope = ava::mcp::McpServerScope::Project,
                                   .source_path = root / "mcp.json"};
  if (!mode.empty()) server.args = {std::move(mode)};
  return server;
}

ava::mcp::McpStdioClientOptions lifecycle_options(std::filesystem::path const& workspace)
{
  ava::mcp::McpStdioClientOptions options;
  options.workspace_dir = workspace;
  options.startup_timeout = std::chrono::milliseconds(500);
  options.request_timeout = std::chrono::milliseconds(500);
  options.max_message_bytes = 64 * 1024;
  options.max_stderr_bytes = 64 * 1024;
  return options;
}

void test_mcp_lifecycle_starts_and_shuts_down_fake_server()
{
  auto const root = fresh_root("mcp-lifecycle-start-shutdown");
  auto const workspace = root / "workspace";

  auto client = ava::mcp::McpStdioClient::start(fake_server_config(root), lifecycle_options(workspace));
  expect(client.has_value(),
         client ? "MCP lifecycle starts fake server" : "MCP lifecycle starts fake server: " + client.error().format());
  if (!client) return;

  expect((*client)->initialization().server_name == "fake-mcp" && (*client)->initialization().server_version == "1.0.0",
         "MCP lifecycle records initialization metadata");
  auto shutdown = (*client)->shutdown(std::chrono::milliseconds(500));
  expect(shutdown.has_value(), shutdown ? "MCP lifecycle shuts down fake server"
                                        : "MCP lifecycle shuts down fake server: " + shutdown.error().format());
}

void test_mcp_lifecycle_reports_startup_exit_status_and_stderr()
{
  auto const root = fresh_root("mcp-lifecycle-startup-exit");
  auto const workspace = root / "workspace";

  auto started =
      ava::mcp::McpStdioClient::start(fake_server_config(root, "exit-initialize"), lifecycle_options(workspace));
  auto const formatted = started ? std::string{} : started.error().format();
  expect(!started && formatted.find("status: exit 42") != std::string::npos &&
             formatted.find("fake MCP exited during initialize") != std::string::npos,
         "MCP lifecycle reports startup exit status and stderr diagnostics: " + formatted);
}

void test_mcp_lifecycle_cancels_hung_startup()
{
  auto const root = fresh_root("mcp-lifecycle-startup-cancel");
  auto const workspace = root / "workspace";

  int cancel_checks = 0;
  auto const started_at = std::chrono::steady_clock::now();
  auto started =
      ava::mcp::McpStdioClient::start(fake_server_config(root, "timeout-initialize"), lifecycle_options(workspace),
                                      [&cancel_checks] { return ++cancel_checks > 2; });
  auto const elapsed = std::chrono::steady_clock::now() - started_at;
  expect(!started && started.error().message().find("canceled") != std::string::npos,
         "MCP lifecycle honors startup cancellation");
  expect(elapsed < std::chrono::seconds(2), "MCP lifecycle cancellation avoids waiting for startup timeout");
}

void test_mcp_lifecycle_bounds_stderr_tail()
{
  auto const root = fresh_root("mcp-lifecycle-stderr");
  auto const workspace = root / "workspace";
  auto options = lifecycle_options(workspace);
  options.max_stderr_bytes = 16;

  auto client = ava::mcp::McpStdioClient::start(fake_server_config(root, "stderr-noise"), options);
  expect(client.has_value(), client ? "MCP lifecycle starts noisy fake server"
                                    : "MCP lifecycle starts noisy fake server: " + client.error().format());
  if (!client) return;

  auto tools = (*client)->list_tools();
  expect(tools.has_value(),
         tools ? "MCP lifecycle drains noisy stderr while reading responses"
               : "MCP lifecycle drains noisy stderr while reading responses: " + tools.error().format());
  auto shutdown = (*client)->shutdown(std::chrono::milliseconds(500));
  expect(shutdown.has_value(), "MCP lifecycle shuts down noisy fake server");
  expect((*client)->stderr_truncated() && (*client)->stderr_tail() == "mcp-stderr-tail!",
         "MCP lifecycle keeps bounded stderr tail diagnostics");
}

}  // namespace

void run_mcp_stdio_client_lifecycle_tests()
{
  test_mcp_lifecycle_starts_and_shuts_down_fake_server();
  test_mcp_lifecycle_reports_startup_exit_status_and_stderr();
  test_mcp_lifecycle_cancels_hung_startup();
  test_mcp_lifecycle_bounds_stderr_tail();
}
