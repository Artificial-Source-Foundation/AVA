#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ava/agent/tool_dispatcher.h"
#include "ava/core/json.h"
#include "ava/lsp/lsp_client.h"
#include "ava/tools/file_tools.h"
#include "ava/tools/lsp_tools.h"
#include "tests/support/test_harness.h"

namespace {

#ifndef AVA_FAKE_LSP_SERVER_PATH
#define AVA_FAKE_LSP_SERVER_PATH ""
#endif

std::vector<std::string> fake_lsp_argv(std::vector<std::string> extra = {}) {
  std::vector<std::string> argv{AVA_FAKE_LSP_SERVER_PATH};
  argv.insert(argv.end(), extra.begin(), extra.end());
  return argv;
}

std::filesystem::path make_lsp_workspace(std::string_view name) {
  auto const workspace = temp_root() / std::string(name);
  std::error_code remove_error;
  std::filesystem::remove_all(workspace, remove_error);
  std::filesystem::create_directories(workspace);
  std::ofstream file(workspace / "main.cpp", std::ios::binary | std::ios::trunc);
  file << "int main() { return 0; }\n";
  return workspace;
}

class ManyDiagnosticsProvider final : public ava::lsp::DiagnosticsProvider {
 public:
  [[nodiscard]] ava::core::Result<std::vector<ava::lsp::Diagnostic>> diagnostics(
      std::filesystem::path const&, ava::lsp::CancelCallback = nullptr) override {
    std::vector<ava::lsp::Diagnostic> diagnostics;
    diagnostics.reserve(300);
    for (int index = 0; index < 300; ++index) {
      diagnostics.push_back(ava::lsp::Diagnostic{
          .severity = 2, .message = std::string(1024, 'x'), .line = index, .column = 1, .code = "MANY"});
    }
    return diagnostics;
  }
};

void test_lsp_manager_fake_server_diagnostics() {
  auto const workspace = make_lsp_workspace("lsp-manager");
  auto client = ava::lsp::SubprocessLspClient::start(ava::lsp::ServerConfig{
      .argv = fake_lsp_argv(),
      .workspace_root = workspace,
      .request_timeout = std::chrono::milliseconds(3000),
  });
  expect(client.has_value(), "LSP manager starts and initializes fake server");
  auto diagnostics = client ? (*client)->diagnostics(workspace / "main.cpp")
                            : ava::core::Result<std::vector<ava::lsp::Diagnostic>>{
                                  std::unexpected(ava::core::Error(ava::core::ErrorCategory::Tool, "missing client"))};
  expect(diagnostics && diagnostics->size() == 1 && (*diagnostics)[0].severity == 1 &&
             (*diagnostics)[0].message == "fake diagnostic from LSP" && (*diagnostics)[0].line == 2 &&
             (*diagnostics)[0].column == 4 && (*diagnostics)[0].code == "AVA_FAKE",
         "LSP manager requests and parses fake diagnostics");
}

void test_lsp_manager_malformed_response_error() {
  auto const workspace = make_lsp_workspace("lsp-malformed");
  auto client = ava::lsp::SubprocessLspClient::start(ava::lsp::ServerConfig{
      .argv = fake_lsp_argv({"--malformed-diagnostics"}),
      .workspace_root = workspace,
      .request_timeout = std::chrono::milliseconds(3000),
  });
  expect(client.has_value(), "LSP manager starts malformed fake server");
  auto diagnostics = client ? (*client)->diagnostics(workspace / "main.cpp")
                            : ava::core::Result<std::vector<ava::lsp::Diagnostic>>{
                                  std::unexpected(ava::core::Error(ava::core::ErrorCategory::Tool, "missing client"))};
  expect(!diagnostics && diagnostics.error().format().find("malformed") != std::string::npos,
         "LSP manager reports malformed diagnostics responses cleanly");
}

void test_lsp_manager_crash_error() {
  auto const workspace = make_lsp_workspace("lsp-crash");
  auto client = ava::lsp::SubprocessLspClient::start(ava::lsp::ServerConfig{
      .argv = fake_lsp_argv({"--crash-diagnostics"}),
      .workspace_root = workspace,
      .request_timeout = std::chrono::milliseconds(3000),
  });
  expect(client.has_value(), "LSP manager starts crashing fake server");
  auto diagnostics = client ? (*client)->diagnostics(workspace / "main.cpp")
                            : ava::core::Result<std::vector<ava::lsp::Diagnostic>>{
                                  std::unexpected(ava::core::Error(ava::core::ErrorCategory::Tool, "missing client"))};
  expect(!diagnostics && diagnostics.error().format().find("LSP server") != std::string::npos,
         "LSP manager reports crashed diagnostics server cleanly");
}

void test_lsp_manager_timeout_error() {
  auto const workspace = make_lsp_workspace("lsp-timeout");
  auto client = ava::lsp::SubprocessLspClient::start(ava::lsp::ServerConfig{
      .argv = fake_lsp_argv({"--sleep-diagnostics"}),
      .workspace_root = workspace,
      .request_timeout = std::chrono::milliseconds(250),
  });
  expect(client.has_value(), "LSP timeout test starts sleeping fake server");
  auto diagnostics = client ? (*client)->diagnostics(workspace / "main.cpp")
                            : ava::core::Result<std::vector<ava::lsp::Diagnostic>>{
                                  std::unexpected(ava::core::Error(ava::core::ErrorCategory::Tool, "missing client"))};
  expect(!diagnostics && diagnostics.error().format().find("timed out") != std::string::npos,
         "LSP manager times out and terminates unresponsive diagnostics requests");
}

void test_lsp_manager_cancellation() {
  auto const startup_workspace = make_lsp_workspace("lsp-startup-cancel");
  int startup_cancel_checks = 0;
  auto startup_canceled = ava::lsp::SubprocessLspClient::start(
      ava::lsp::ServerConfig{
          .argv = fake_lsp_argv({"--sleep-initialize"}),
          .workspace_root = startup_workspace,
          .request_timeout = std::chrono::milliseconds(1000),
      },
      [&] { return ++startup_cancel_checks > 2; });
  expect(!startup_canceled && startup_canceled.error().message().find("canceled") != std::string::npos,
         "LSP manager cancels hung startup before timeout");

  auto const diagnostics_workspace = make_lsp_workspace("lsp-diagnostics-cancel");
  auto client = ava::lsp::SubprocessLspClient::start(ava::lsp::ServerConfig{
      .argv = fake_lsp_argv({"--sleep-diagnostics"}),
      .workspace_root = diagnostics_workspace,
      .request_timeout = std::chrono::milliseconds(1000),
  });
  expect(client.has_value(), "LSP cancellation test starts sleeping fake server");
  if (client) {
    int cancel_checks = 0;
    auto diagnostics = (*client)->diagnostics(diagnostics_workspace / "main.cpp", [&] { return ++cancel_checks > 2; });
    expect(!diagnostics && diagnostics.error().message().find("canceled") != std::string::npos,
           "LSP manager cancels hung diagnostics before timeout");
  }
}

void test_lsp_manager_huge_response_caps() {
  auto const content_workspace = make_lsp_workspace("lsp-huge-content-length");
  auto content_client = ava::lsp::SubprocessLspClient::start(ava::lsp::ServerConfig{
      .argv = fake_lsp_argv({"--huge-content-length"}),
      .workspace_root = content_workspace,
      .request_timeout = std::chrono::milliseconds(3000),
  });
  expect(content_client.has_value(), "LSP huge content-length test starts fake server");
  auto content_result = content_client ? (*content_client)->diagnostics(content_workspace / "main.cpp")
                                       : ava::core::Result<std::vector<ava::lsp::Diagnostic>>{std::unexpected(
                                             ava::core::Error(ava::core::ErrorCategory::Tool, "missing client"))};
  expect(!content_result &&
             content_result.error().format().find("Content-Length exceeds message cap") != std::string::npos,
         "LSP manager rejects oversized Content-Length before reading the body");

  auto const header_workspace = make_lsp_workspace("lsp-huge-header");
  auto header_client = ava::lsp::SubprocessLspClient::start(ava::lsp::ServerConfig{
      .argv = fake_lsp_argv({"--huge-header"}),
      .workspace_root = header_workspace,
      .request_timeout = std::chrono::milliseconds(3000),
  });
  expect(header_client.has_value(), "LSP huge header test starts fake server");
  auto header_result = header_client ? (*header_client)->diagnostics(header_workspace / "main.cpp")
                                     : ava::core::Result<std::vector<ava::lsp::Diagnostic>>{std::unexpected(
                                           ava::core::Error(ava::core::ErrorCategory::Tool, "missing client"))};
  expect(!header_result && header_result.error().format().find("header exceeds size cap") != std::string::npos,
         "LSP manager rejects oversized response headers");
}

void test_lsp_diagnostics_tool_and_dispatcher_json() {
  auto const workspace = make_lsp_workspace("lsp-tool");
  auto client = ava::lsp::SubprocessLspClient::start(ava::lsp::ServerConfig{
      .argv = fake_lsp_argv(),
      .workspace_root = workspace,
      .request_timeout = std::chrono::milliseconds(3000),
  });
  expect(client.has_value(), "LSP tool test starts fake server provider");
  ava::tools::ToolContext const context{
      .workspace_dir = workspace,
      .lsp_diagnostics_provider = client ? std::shared_ptr<ava::lsp::DiagnosticsProvider>(*client) : nullptr};
  auto tool = ava::tools::lsp_diagnostics(context, workspace / "main.cpp");
  expect(tool && tool->diagnostics.size() == 1 && tool->diagnostics[0].message == "fake diagnostic from LSP",
         "lsp_diagnostics tool returns structured diagnostics");

  ava::agent::ToolDispatcher const dispatcher(context);
  auto dispatched = dispatcher.dispatch(ava::agent::ProviderToolCall{
      .id = "call_lsp", .name = "lsp_diagnostics", .arguments_json = "{\"path\":\"main.cpp\"}"});
  auto const path = dispatched ? ava::core::json::string_field(dispatched->result_text, "path") : std::nullopt;
  expect(dispatched && dispatched->success && path && *path == "main.cpp" &&
             dispatched->result_text.find("\"tool\":\"lsp_diagnostics\"") != std::string::npos &&
             dispatched->result_text.find("\"severity\":1") != std::string::npos &&
             dispatched->result_text.find("fake diagnostic from LSP") != std::string::npos &&
             dispatched->result_text.find("\"code\":\"AVA_FAKE\"") != std::string::npos,
         "tool dispatcher returns expected lsp_diagnostics JSON");

  auto canceled_context = context;
  canceled_context.cancel_requested = [] { return true; };
  ava::agent::ToolDispatcher const canceled_dispatcher(canceled_context);
  auto canceled = canceled_dispatcher.dispatch(ava::agent::ProviderToolCall{
      .id = "call_lsp_canceled", .name = "lsp_diagnostics", .arguments_json = "{\"path\":\"main.cpp\"}"});
  expect(canceled && !canceled->success && canceled->payload.status == ava::agent::ToolResultStatus::Canceled &&
             canceled->result_text.find("canceled") != std::string::npos,
         "tool dispatcher preserves semantic cancellation for lsp_diagnostics");
}

void test_lsp_file_uri_escapes_encoded_separators() {
  auto const workspace = make_lsp_workspace("lsp-uri-escape");
  auto const literal_name = std::string("x%2F..%2F..%2F.env");
  std::ofstream file(workspace / literal_name, std::ios::binary | std::ios::trunc);
  file << "literal percent path\n";

  auto client = ava::lsp::SubprocessLspClient::start(ava::lsp::ServerConfig{
      .argv = fake_lsp_argv({"--echo-uri-diagnostics"}),
      .workspace_root = workspace,
      .request_timeout = std::chrono::milliseconds(3000),
  });
  expect(client.has_value(), "LSP URI escaping test starts fake server");
  ava::tools::ToolContext const context{
      .workspace_dir = workspace,
      .lsp_diagnostics_provider = client ? std::shared_ptr<ava::lsp::DiagnosticsProvider>(*client) : nullptr};
  auto tool = ava::tools::lsp_diagnostics(context, workspace / literal_name);
  expect(tool && tool->diagnostics.size() == 1 &&
             tool->diagnostics[0].message.find("%252F..%252F..%252F.env") != std::string::npos &&
             tool->diagnostics[0].message.find("x%2F..%2F") == std::string::npos,
         "LSP file URIs percent-encode literal percent sequences before server parsing");
}

void test_lsp_dispatcher_redacts_server_error_context() {
  auto const workspace = make_lsp_workspace("lsp-redacted-error");
  auto client = ava::lsp::SubprocessLspClient::start(ava::lsp::ServerConfig{
      .argv = fake_lsp_argv({"--crash-diagnostics"}),
      .workspace_root = workspace,
      .request_timeout = std::chrono::milliseconds(3000),
  });
  expect(client.has_value(), "LSP redaction test starts crashing fake server");
  ava::tools::ToolContext const context{
      .workspace_dir = workspace,
      .lsp_diagnostics_provider = client ? std::shared_ptr<ava::lsp::DiagnosticsProvider>(*client) : nullptr};
  ava::agent::ToolDispatcher const dispatcher(context);
  auto dispatched = dispatcher.dispatch(ava::agent::ProviderToolCall{
      .id = "call_lsp_redacted", .name = "lsp_diagnostics", .arguments_json = "{\"path\":\"main.cpp\"}"});
  expect(dispatched && !dispatched->success &&
             dispatched->result_text.find("LSP diagnostics failed") != std::string::npos &&
             dispatched->result_text.find(AVA_FAKE_LSP_SERVER_PATH) == std::string::npos &&
             dispatched->result_text.find(workspace.generic_string()) == std::string::npos,
         "provider-facing LSP errors redact local server command and workspace context");
}

void test_lsp_dispatcher_bounds_provider_json() {
  auto const workspace = make_lsp_workspace("lsp-bounded-json");
  ava::tools::ToolContext const context{.workspace_dir = workspace,
                                        .lsp_diagnostics_provider = std::make_shared<ManyDiagnosticsProvider>()};
  ava::agent::ToolDispatcher const dispatcher(context);
  auto dispatched = dispatcher.dispatch(ava::agent::ProviderToolCall{
      .id = "call_lsp_many", .name = "lsp_diagnostics", .arguments_json = "{\"path\":\"main.cpp\"}"});
  expect(dispatched && dispatched->success && dispatched->result_text.size() <= 64 * 1024 &&
             dispatched->result_text.find("\"truncated\":true") != std::string::npos &&
             dispatched->result_text.find("\"total_diagnostics\":300") != std::string::npos,
         "lsp_diagnostics provider JSON is bounded and reports diagnostic truncation");

  auto long_path = std::string(5000, 'a');
  auto long_path_result = dispatcher.dispatch(ava::agent::ProviderToolCall{
      .id = "call_lsp_long_path",
      .name = "lsp_diagnostics",
      .arguments_json = "{\"path\":\"" + long_path + "\"}",
  });
  expect(long_path_result && !long_path_result->success &&
             long_path_result->result_text.find("path is too long") != std::string::npos &&
             long_path_result->result_text.size() <= 64 * 1024,
         "lsp_diagnostics rejects oversized provider path arguments before JSON reflection can exceed the cap");
}

}  // namespace

void run_lsp_tests() {
  test_lsp_manager_fake_server_diagnostics();
  test_lsp_manager_malformed_response_error();
  test_lsp_manager_crash_error();
  test_lsp_manager_timeout_error();
  test_lsp_manager_cancellation();
  test_lsp_manager_huge_response_caps();
  test_lsp_diagnostics_tool_and_dispatcher_json();
  test_lsp_file_uri_escapes_encoded_separators();
  test_lsp_dispatcher_redacts_server_error_context();
  test_lsp_dispatcher_bounds_provider_json();
}
