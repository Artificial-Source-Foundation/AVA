#include "sys.h"
#include "tests/support/test_harness.h"
#include "ava/agent/tool_dispatcher.h"
#include "ava/tools/file_tools.h"
#include "ava/tools/lsp_tools.h"
#include "ava/lsp/configured_provider.h"
#include "ava/lsp/lsp_client.h"
#include "ava/core/json.h"

#include <cerrno>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#include <signal.h>
#include <sys/types.h>

namespace {

#ifndef AVA_FAKE_LSP_SERVER_PATH
#define AVA_FAKE_LSP_SERVER_PATH ""
#endif

std::vector<std::string> fake_lsp_argv(std::vector<std::string> extra = {})
{
  std::vector<std::string> argv{AVA_FAKE_LSP_SERVER_PATH};
  argv.insert(argv.end(), extra.begin(), extra.end());
  return argv;
}

std::filesystem::path make_lsp_workspace(std::string_view name)
{
  auto const workspace = temp_root() / std::string(name);
  std::error_code remove_error;
  std::filesystem::remove_all(workspace, remove_error);
  std::filesystem::create_directories(workspace);
  std::ofstream file(workspace / "main.cpp", std::ios::binary | std::ios::trunc);
  file << "int main() { return 0; }\n";
  return workspace;
}

std::string read_text_file_for_test(std::filesystem::path const& path)
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

class ManyDiagnosticsProvider final : public ava::lsp::DiagnosticsProvider
{
 public:
  [[nodiscard]] ava::core::Result<std::vector<ava::lsp::Diagnostic>> diagnostics(std::filesystem::path const&, ava::lsp::CancelCallback = nullptr) override
  {
    std::vector<ava::lsp::Diagnostic> diagnostics;
    diagnostics.reserve(300);
    for (int index = 0; index < 300; ++index)
    {
      diagnostics.push_back(ava::lsp::Diagnostic{.severity = 2, .message = std::string(1024, 'x'), .line = index, .column = 1, .code = "MANY"});
    }
    return diagnostics;
  }
};

class ManySymbolsProvider final : public ava::lsp::DiagnosticsProvider
{
 public:
  explicit ManySymbolsProvider(std::filesystem::path workspace) : workspace_(std::move(workspace)) { }

  [[nodiscard]] ava::core::Result<std::vector<ava::lsp::Diagnostic>> diagnostics(std::filesystem::path const&, ava::lsp::CancelCallback = nullptr) override
  {
    return std::vector<ava::lsp::Diagnostic>{};
  }

  [[nodiscard]] ava::core::Result<std::vector<ava::lsp::Symbol>> document_symbols(std::filesystem::path const&, ava::lsp::CancelCallback = nullptr) override
  {
    std::vector<ava::lsp::Symbol> symbols;
    symbols.reserve(300);
    for (int index = 0; index < 300; ++index)
    {
      symbols.push_back(ava::lsp::Symbol{.name = std::string(1024, 's'),
                                         .kind = 12,
                                         .path = workspace_ / "main.cpp",
                                         .range = ava::lsp::Range{.start_line = index, .start_column = 0, .end_line = index, .end_column = 4},
                                         .container = "many"});
    }
    return symbols;
  }

 private:
  std::filesystem::path workspace_;
};

void test_lsp_manager_fake_server_diagnostics()
{
  auto const workspace = make_lsp_workspace("lsp-manager");
  auto client = ava::lsp::SubprocessLspClient::start(ava::lsp::ServerConfig{
      .argv = fake_lsp_argv(),
      .workspace_root = workspace,
      .process_cwd = workspace,
      .request_timeout = std::chrono::milliseconds(3000),
  });
  expect(client.has_value(), "LSP manager starts and initializes fake server");
  auto diagnostics =
      client ? (*client)->diagnostics(workspace / "main.cpp")
             : ava::core::Result<std::vector<ava::lsp::Diagnostic>>{std::unexpected(ava::core::Error(ava::core::ErrorCategory::Tool, "missing client"))};
  expect(diagnostics && diagnostics->size() == 1 && (*diagnostics)[0].severity == 1 && (*diagnostics)[0].message == "fake diagnostic from LSP" &&
             (*diagnostics)[0].line == 2 && (*diagnostics)[0].column == 4 && (*diagnostics)[0].code == "AVA_FAKE",
         "LSP manager requests and parses fake diagnostics");
}

void test_lsp_manager_fake_server_symbols_and_definition()
{
  auto const workspace = make_lsp_workspace("lsp-symbols");
  auto client = ava::lsp::SubprocessLspClient::start(ava::lsp::ServerConfig{
      .argv = fake_lsp_argv(),
      .workspace_root = workspace,
      .process_cwd = workspace,
      .request_timeout = std::chrono::milliseconds(3000),
  });
  expect(client.has_value(), "LSP symbols test starts fake server");
  if (!client)
    return;

  auto document_symbols = (*client)->document_symbols(workspace / "main.cpp");
  expect(document_symbols && document_symbols->size() == 2 && (*document_symbols)[0].name == "main" && (*document_symbols)[0].path == workspace / "main.cpp" &&
             (*document_symbols)[1].container == "main",
         "LSP manager requests and parses document symbols");

  auto workspace_symbols = (*client)->workspace_symbols("main");
  expect(workspace_symbols && workspace_symbols->size() == 1 && (*workspace_symbols)[0].name == "main" && (*workspace_symbols)[0].container == "global" &&
             (*workspace_symbols)[0].path == workspace / "main.cpp",
         "LSP manager requests and parses workspace symbols");

  auto definitions = (*client)->definitions(workspace / "main.cpp", 0, 4);
  expect(definitions && definitions->size() == 1 && (*definitions)[0].path == workspace / "main.cpp" && (*definitions)[0].range.start_line == 0 &&
             (*definitions)[0].range.start_column == 4,
         "LSP manager requests and parses definitions");

  auto references = (*client)->references(workspace / "main.cpp", 0, 4);
  expect(references && references->size() == 2 && (*references)[0].path == workspace / "main.cpp" && (*references)[1].range.start_column == 13,
         "LSP manager sends didOpen before references and parses locations");
}

void test_lsp_manager_malformed_symbols_error()
{
  auto const workspace = make_lsp_workspace("lsp-malformed-symbols");
  auto client = ava::lsp::SubprocessLspClient::start(ava::lsp::ServerConfig{
      .argv = fake_lsp_argv({"--malformed-symbols"}),
      .workspace_root = workspace,
      .process_cwd = workspace,
      .request_timeout = std::chrono::milliseconds(3000),
  });
  expect(client.has_value(), "LSP malformed symbols test starts fake server");
  auto symbols = client ? (*client)->document_symbols(workspace / "main.cpp")
                        : ava::core::Result<std::vector<ava::lsp::Symbol>>{std::unexpected(ava::core::Error(ava::core::ErrorCategory::Tool, "missing client"))};
  expect(!symbols && symbols.error().format().find("malformed") != std::string::npos, "LSP manager reports malformed symbol responses cleanly");
}

void test_lsp_manager_malformed_response_error()
{
  auto const workspace = make_lsp_workspace("lsp-malformed");
  auto client = ava::lsp::SubprocessLspClient::start(ava::lsp::ServerConfig{
      .argv = fake_lsp_argv({"--malformed-diagnostics"}),
      .workspace_root = workspace,
      .process_cwd = workspace,
      .request_timeout = std::chrono::milliseconds(3000),
  });
  expect(client.has_value(), "LSP manager starts malformed fake server");
  auto diagnostics =
      client ? (*client)->diagnostics(workspace / "main.cpp")
             : ava::core::Result<std::vector<ava::lsp::Diagnostic>>{std::unexpected(ava::core::Error(ava::core::ErrorCategory::Tool, "missing client"))};
  expect(!diagnostics && diagnostics.error().format().find("malformed") != std::string::npos, "LSP manager reports malformed diagnostics responses cleanly");
}

void test_lsp_manager_crash_error()
{
  auto const workspace = make_lsp_workspace("lsp-crash");
  auto client = ava::lsp::SubprocessLspClient::start(ava::lsp::ServerConfig{
      .argv = fake_lsp_argv({"--crash-diagnostics"}),
      .workspace_root = workspace,
      .process_cwd = workspace,
      .request_timeout = std::chrono::milliseconds(3000),
  });
  expect(client.has_value(), "LSP manager starts crashing fake server");
  auto diagnostics =
      client ? (*client)->diagnostics(workspace / "main.cpp")
             : ava::core::Result<std::vector<ava::lsp::Diagnostic>>{std::unexpected(ava::core::Error(ava::core::ErrorCategory::Tool, "missing client"))};
  expect(!diagnostics && diagnostics.error().format().find("LSP server") != std::string::npos, "LSP manager reports crashed diagnostics server cleanly");
}

void test_lsp_manager_timeout_error()
{
  auto const workspace = make_lsp_workspace("lsp-timeout");
  auto client = ava::lsp::SubprocessLspClient::start(ava::lsp::ServerConfig{
      .argv = fake_lsp_argv({"--sleep-diagnostics"}),
      .workspace_root = workspace,
      .process_cwd = workspace,
      .request_timeout = std::chrono::milliseconds(250),
  });
  expect(client.has_value(), "LSP timeout test starts sleeping fake server");
  auto diagnostics =
      client ? (*client)->diagnostics(workspace / "main.cpp")
             : ava::core::Result<std::vector<ava::lsp::Diagnostic>>{std::unexpected(ava::core::Error(ava::core::ErrorCategory::Tool, "missing client"))};
  expect(!diagnostics && diagnostics.error().format().find("timed out") != std::string::npos,
         "LSP manager times out and terminates unresponsive diagnostics requests");
}

void test_lsp_manager_cancellation()
{
  auto const startup_workspace = make_lsp_workspace("lsp-startup-cancel");
  auto const startup_cancel_pgid_file = startup_workspace / "lsp-startup-cancel-pgid.txt";
  auto startup_canceled = ava::lsp::SubprocessLspClient::start(
      ava::lsp::ServerConfig{
          .argv = fake_lsp_argv({"--sleep-initialize-marker", startup_cancel_pgid_file.generic_string()}),
          .workspace_root = startup_workspace,
          .process_cwd = startup_workspace,
          .request_timeout = std::chrono::milliseconds(1000),
      },
      [&] { return read_pid_file_for_test(startup_cancel_pgid_file).has_value(); });
  auto const startup_cancel_pgid = read_pid_file_for_test(startup_cancel_pgid_file);
  expect(!startup_canceled && startup_canceled.error().message().find("canceled") != std::string::npos && startup_cancel_pgid &&
             wait_for_process_group_exit(*startup_cancel_pgid),
         "LSP manager cancels hung startup and terminates the server process group before timeout");

  auto const diagnostics_workspace = make_lsp_workspace("lsp-diagnostics-cancel");
  auto const diagnostics_cancel_pgid_file = diagnostics_workspace / "lsp-diagnostics-cancel-pgid.txt";
  auto client = ava::lsp::SubprocessLspClient::start(ava::lsp::ServerConfig{
      .argv = fake_lsp_argv({"--sleep-diagnostics-marker", diagnostics_cancel_pgid_file.generic_string()}),
      .workspace_root = diagnostics_workspace,
      .process_cwd = diagnostics_workspace,
      .request_timeout = std::chrono::milliseconds(1000),
  });
  expect(client.has_value(), "LSP cancellation test starts sleeping fake server");
  if (client)
  {
    auto diagnostics =
        (*client)->diagnostics(diagnostics_workspace / "main.cpp", [&] { return read_pid_file_for_test(diagnostics_cancel_pgid_file).has_value(); });
    auto const diagnostics_cancel_pgid = read_pid_file_for_test(diagnostics_cancel_pgid_file);
    expect(!diagnostics && diagnostics.error().message().find("canceled") != std::string::npos && diagnostics_cancel_pgid &&
               wait_for_process_group_exit(*diagnostics_cancel_pgid),
           "LSP manager cancels hung diagnostics and terminates the server process group before timeout");
  }
}

void test_lsp_manager_huge_response_caps()
{
  auto const content_workspace = make_lsp_workspace("lsp-huge-content-length");
  auto content_client = ava::lsp::SubprocessLspClient::start(ava::lsp::ServerConfig{
      .argv = fake_lsp_argv({"--huge-content-length"}),
      .workspace_root = content_workspace,
      .process_cwd = content_workspace,
      .request_timeout = std::chrono::milliseconds(3000),
  });
  expect(content_client.has_value(), "LSP huge content-length test starts fake server");
  auto content_result =
      content_client
          ? (*content_client)->diagnostics(content_workspace / "main.cpp")
          : ava::core::Result<std::vector<ava::lsp::Diagnostic>>{std::unexpected(ava::core::Error(ava::core::ErrorCategory::Tool, "missing client"))};
  expect(!content_result && content_result.error().format().find("Content-Length exceeds message cap") != std::string::npos,
         "LSP manager rejects oversized Content-Length before reading the body");

  auto const header_workspace = make_lsp_workspace("lsp-huge-header");
  auto header_client = ava::lsp::SubprocessLspClient::start(ava::lsp::ServerConfig{
      .argv = fake_lsp_argv({"--huge-header"}),
      .workspace_root = header_workspace,
      .process_cwd = header_workspace,
      .request_timeout = std::chrono::milliseconds(3000),
  });
  expect(header_client.has_value(), "LSP huge header test starts fake server");
  auto header_result =
      header_client ? (*header_client)->diagnostics(header_workspace / "main.cpp")
                    : ava::core::Result<std::vector<ava::lsp::Diagnostic>>{std::unexpected(ava::core::Error(ava::core::ErrorCategory::Tool, "missing client"))};
  expect(!header_result && header_result.error().format().find("header exceeds size cap") != std::string::npos,
         "LSP manager rejects oversized response headers");
}

void test_lsp_diagnostics_tool_and_dispatcher_json()
{
  auto const workspace = make_lsp_workspace("lsp-tool");
  auto client = ava::lsp::SubprocessLspClient::start(ava::lsp::ServerConfig{
      .argv = fake_lsp_argv(),
      .workspace_root = workspace,
      .process_cwd = workspace,
      .request_timeout = std::chrono::milliseconds(3000),
  });
  expect(client.has_value(), "LSP tool test starts fake server provider");
  ava::tools::ToolContext const context{.workspace_dir = workspace,
                                        .lsp_diagnostics_provider = client ? std::shared_ptr<ava::lsp::DiagnosticsProvider>(*client) : nullptr};
  auto tool = ava::tools::lsp_diagnostics(context, workspace / "main.cpp");
  expect(tool && tool->diagnostics.size() == 1 && tool->diagnostics[0].message == "fake diagnostic from LSP",
         "lsp_diagnostics tool returns structured diagnostics");

  ava::agent::ToolDispatcher const dispatcher(context);
  auto dispatched = dispatcher.dispatch(ava::agent::ProviderToolCall{.id = "call_lsp", .name = "lsp_diagnostics", .arguments_json = "{\"path\":\"main.cpp\"}"});
  auto const path = dispatched ? ava::core::json::string_field(dispatched->result_text, "path") : std::nullopt;
  expect(dispatched && dispatched->success && path && *path == "main.cpp" &&
             dispatched->result_text.find("\"tool\":\"lsp_diagnostics\"") != std::string::npos &&
             dispatched->result_text.find("\"severity\":1") != std::string::npos &&
             dispatched->result_text.find("fake diagnostic from LSP") != std::string::npos &&
             dispatched->result_text.find("\"code\":\"AVA_FAKE\"") != std::string::npos,
         "tool dispatcher returns expected lsp_diagnostics JSON");

  auto document_symbols =
      dispatcher.dispatch(ava::agent::ProviderToolCall{.id = "call_lsp_symbols", .name = "lsp_document_symbols", .arguments_json = "{\"path\":\"main.cpp\"}"});
  expect(document_symbols && document_symbols->success && document_symbols->result_text.find("\"tool\":\"lsp_document_symbols\"") != std::string::npos &&
             document_symbols->result_text.find("\"name\":\"main\"") != std::string::npos &&
             document_symbols->result_text.find("\"container\":\"main\"") != std::string::npos,
         "tool dispatcher returns expected lsp_document_symbols JSON");

  auto workspace_symbols =
      dispatcher.dispatch(ava::agent::ProviderToolCall{.id = "call_lsp_workspace", .name = "lsp_workspace_symbols", .arguments_json = "{\"query\":\"main\"}"});
  expect(workspace_symbols && workspace_symbols->success && workspace_symbols->result_text.find("\"tool\":\"lsp_workspace_symbols\"") != std::string::npos &&
             workspace_symbols->result_text.find("\"query\":\"main\"") != std::string::npos &&
             workspace_symbols->result_text.find("\"path\":\"main.cpp\"") != std::string::npos,
         "tool dispatcher returns expected lsp_workspace_symbols JSON");

  auto definition = dispatcher.dispatch(
      ava::agent::ProviderToolCall{.id = "call_lsp_definition", .name = "lsp_definition", .arguments_json = "{\"path\":\"main.cpp\",\"line\":0,\"column\":4}"});
  expect(definition && definition->success && definition->result_text.find("\"tool\":\"lsp_definition\"") != std::string::npos &&
             definition->result_text.find("\"total_locations\":1") != std::string::npos &&
             definition->result_text.find("\"path\":\"main.cpp\"") != std::string::npos,
         "tool dispatcher returns expected lsp_definition JSON");

  auto references = dispatcher.dispatch(
      ava::agent::ProviderToolCall{.id = "call_lsp_references", .name = "lsp_references", .arguments_json = "{\"path\":\"main.cpp\",\"line\":0,\"column\":4}"});
  expect(references && references->success && references->result_text.find("\"tool\":\"lsp_references\"") != std::string::npos &&
             references->result_text.find("\"total_locations\":2") != std::string::npos &&
             references->result_text.find("\"path\":\"main.cpp\"") != std::string::npos,
         "tool dispatcher returns expected lsp_references JSON");

  auto canceled_context = context;
  canceled_context.cancel_requested = [] { return true; };
  ava::agent::ToolDispatcher const canceled_dispatcher(canceled_context);
  auto canceled = canceled_dispatcher.dispatch(
      ava::agent::ProviderToolCall{.id = "call_lsp_canceled", .name = "lsp_diagnostics", .arguments_json = "{\"path\":\"main.cpp\"}"});
  expect(canceled && !canceled->success && canceled->payload.status == ava::agent::ToolResultStatus::Canceled &&
             canceled->result_text.find("canceled") != std::string::npos,
         "tool dispatcher preserves semantic cancellation for lsp_diagnostics");
}

void test_lsp_file_uri_escapes_encoded_separators()
{
  auto const workspace = make_lsp_workspace("lsp-uri-escape");
  auto const literal_name = std::string("x%2F..%2F..%2F.env");
  std::ofstream file(workspace / literal_name, std::ios::binary | std::ios::trunc);
  file << "literal percent path\n";

  auto client = ava::lsp::SubprocessLspClient::start(ava::lsp::ServerConfig{
      .argv = fake_lsp_argv({"--echo-uri-diagnostics"}),
      .workspace_root = workspace,
      .process_cwd = workspace,
      .request_timeout = std::chrono::milliseconds(3000),
  });
  expect(client.has_value(), "LSP URI escaping test starts fake server");
  ava::tools::ToolContext const context{.workspace_dir = workspace,
                                        .lsp_diagnostics_provider = client ? std::shared_ptr<ava::lsp::DiagnosticsProvider>(*client) : nullptr};
  auto tool = ava::tools::lsp_diagnostics(context, workspace / literal_name);
  expect(tool && tool->diagnostics.size() == 1 && tool->diagnostics[0].message.find("%252F..%252F..%252F.env") != std::string::npos &&
             tool->diagnostics[0].message.find("x%2F..%2F") == std::string::npos,
         "LSP file URIs percent-encode literal percent sequences before server parsing");
}

void test_lsp_dispatcher_redacts_server_error_context()
{
  auto const workspace = make_lsp_workspace("lsp-redacted-error");
  auto client = ava::lsp::SubprocessLspClient::start(ava::lsp::ServerConfig{
      .argv = fake_lsp_argv({"--crash-diagnostics"}),
      .workspace_root = workspace,
      .process_cwd = workspace,
      .request_timeout = std::chrono::milliseconds(3000),
  });
  expect(client.has_value(), "LSP redaction test starts crashing fake server");
  ava::tools::ToolContext const context{.workspace_dir = workspace,
                                        .lsp_diagnostics_provider = client ? std::shared_ptr<ava::lsp::DiagnosticsProvider>(*client) : nullptr};
  ava::agent::ToolDispatcher const dispatcher(context);
  auto dispatched =
      dispatcher.dispatch(ava::agent::ProviderToolCall{.id = "call_lsp_redacted", .name = "lsp_diagnostics", .arguments_json = "{\"path\":\"main.cpp\"}"});
  expect(dispatched && !dispatched->success && dispatched->result_text.find("LSP query failed") != std::string::npos &&
             dispatched->result_text.find(AVA_FAKE_LSP_SERVER_PATH) == std::string::npos &&
             dispatched->result_text.find(workspace.generic_string()) == std::string::npos,
         "provider-facing LSP errors redact local server command and workspace context");
}

void test_lsp_dispatcher_bounds_provider_json()
{
  auto const workspace = make_lsp_workspace("lsp-bounded-json");
  ava::tools::ToolContext const context{.workspace_dir = workspace, .lsp_diagnostics_provider = std::make_shared<ManyDiagnosticsProvider>()};
  ava::agent::ToolDispatcher const dispatcher(context);
  auto dispatched =
      dispatcher.dispatch(ava::agent::ProviderToolCall{.id = "call_lsp_many", .name = "lsp_diagnostics", .arguments_json = "{\"path\":\"main.cpp\"}"});
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
  expect(long_path_result && !long_path_result->success && long_path_result->result_text.find("path is too long") != std::string::npos &&
             long_path_result->result_text.size() <= 64 * 1024,
         "lsp_diagnostics rejects oversized provider path arguments before JSON reflection can exceed the cap");

  ava::tools::ToolContext const symbol_context{.workspace_dir = workspace, .lsp_diagnostics_provider = std::make_shared<ManySymbolsProvider>(workspace)};
  ava::agent::ToolDispatcher const symbol_dispatcher(symbol_context);
  auto many_symbols = symbol_dispatcher.dispatch(
      ava::agent::ProviderToolCall{.id = "call_lsp_many_symbols", .name = "lsp_document_symbols", .arguments_json = "{\"path\":\"main.cpp\"}"});
  expect(many_symbols && many_symbols->success && many_symbols->result_text.size() <= 64 * 1024 &&
             many_symbols->result_text.find("\"truncated\":true") != std::string::npos &&
             many_symbols->result_text.find("\"total_symbols\":300") != std::string::npos,
         "lsp_document_symbols provider JSON is bounded and reports symbol truncation");
}

void test_lsp_configured_provider_loads_project_config_lazily()
{
  auto const workspace = make_lsp_workspace("lsp-configured-provider");
  std::filesystem::create_directories(workspace / ".ava");
  std::ofstream config(workspace / ".ava" / "lsp.json", std::ios::binary | std::ios::trunc);
  config << "{\"version\":1,\"servers\":[{\"id\":\"fake\",\"argv\":[\"" << ava::core::json::escape(AVA_FAKE_LSP_SERVER_PATH)
         << "\"],\"file_extensions\":[\".cpp\"],\"timeout_ms\":3000}]}";
  config.close();

  auto provider = ava::lsp::make_configured_lsp_provider(ava::lsp::ConfiguredLspProviderFiles{
      .global_config_file = workspace / "missing-global-lsp.json",
      .project_config_file = workspace / ".ava" / "lsp.json",
      .workspace_root = workspace,
      .permission_resolver = [](ava::permissions::PermissionPrompt const& prompt) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
        expect(prompt.operation == ava::permissions::Operation::LspServerLaunch, "configured LSP provider requests explicit launch permission");
        expect(!prompt.permission_request_id.empty(), "configured LSP launch prompts include a permission request id");
        expect(prompt.command.rfind("[\"", 0) == 0 && prompt.command.find(AVA_FAKE_LSP_SERVER_PATH) != std::string::npos,
               "configured LSP launch permission binds a JSON-array argv command");
        return ava::permissions::PermissionResolutionDecision{ava::permissions::PermissionResolution::Allow, "test allow"};
      },
  });
  expect(provider && *provider != nullptr, provider ? "configured LSP provider loads explicit project config"
                                                    : "configured LSP provider loads explicit project config: " + provider.error().format());

  ava::tools::ToolContext const context{.workspace_dir = workspace, .lsp_diagnostics_provider = provider ? *provider : nullptr};
  auto schemas = ava::agent::ToolDispatcher::tool_schemas_json(context);
  bool saw_lsp_references = false;
  for (auto const& schema : schemas)
  {
    if (schema.find("lsp_references") != std::string::npos)
      saw_lsp_references = true;
  }
  expect(saw_lsp_references, "configured LSP provider exposes LSP schemas without launching eagerly");

  ava::agent::ToolDispatcher const dispatcher(context);
  auto references = dispatcher.dispatch(ava::agent::ProviderToolCall{
      .id = "call_configured_refs", .name = "lsp_references", .arguments_json = "{\"path\":\"main.cpp\",\"line\":0,\"column\":4}"});
  expect(references && references->success && references->result_text.find("\"total_locations\":2") != std::string::npos,
         "configured LSP provider launches lazily and dispatches references");
  expect(references && !references->payload.permission_request_ids.empty(),
         "configured LSP launch permission request id is attached to the tool result payload");

  auto no_match = dispatcher.dispatch(
      ava::agent::ProviderToolCall{.id = "call_configured_no_match", .name = "lsp_diagnostics", .arguments_json = "{\"path\":\"README.md\"}"});
  expect(no_match && !no_match->success && no_match->result_text.find("LSP query failed") != std::string::npos,
         "configured LSP provider rejects unmatched file extensions through redacted tool error");
}

void test_lsp_configured_provider_loads_global_config_from_safe_cwd()
{
  auto const root = temp_root() / "lsp-global-safe-cwd";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const global_config_dir = root / "global-config";
  std::filesystem::create_directories(workspace);
  std::filesystem::create_directories(global_config_dir);
  std::ofstream source(workspace / "main.cpp", std::ios::binary | std::ios::trunc);
  source << "int main() { return 0; }\n";
  source.close();

  auto const marker_path = root / "lsp-global-cwd.txt";
  auto const global_config_path = global_config_dir / "lsp.json";
  std::ofstream config(global_config_path, std::ios::binary | std::ios::trunc);
  config << "{\"version\":1,\"servers\":[{\"id\":\"fake\",\"argv\":[\"" << ava::core::json::escape(AVA_FAKE_LSP_SERVER_PATH) << "\",\"--cwd-marker\",\""
         << ava::core::json::escape(marker_path.generic_string()) << "\"],\"file_extensions\":[\".cpp\"]}]}";
  config.close();

  auto provider = ava::lsp::make_configured_lsp_provider(ava::lsp::ConfiguredLspProviderFiles{
      .global_config_file = global_config_path,
      .project_config_file = workspace / "missing-project-lsp.json",
      .workspace_root = workspace,
      .permission_resolver = [](ava::permissions::PermissionPrompt const&) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
        return ava::permissions::PermissionResolutionDecision{ava::permissions::PermissionResolution::Allow, "test allow"};
      },
  });
  expect(provider && *provider != nullptr, provider ? "configured LSP provider loads explicit global config"
                                                    : "configured LSP provider loads explicit global config: " + provider.error().format());
  ava::tools::ToolContext const context{.workspace_dir = workspace, .lsp_diagnostics_provider = provider ? *provider : nullptr};
  ava::agent::ToolDispatcher const dispatcher(context);
  auto diagnostics =
      dispatcher.dispatch(ava::agent::ProviderToolCall{.id = "call_global_cwd", .name = "lsp_diagnostics", .arguments_json = "{\"path\":\"main.cpp\"}"});
  expect(diagnostics && diagnostics->success,
         diagnostics ? "configured global LSP server returns diagnostics from safe cwd" : "configured global LSP server returns diagnostics from safe cwd");
  expect(read_text_file_for_test(marker_path) == std::filesystem::weakly_canonical(global_config_dir).string(),
         "configured global LSP server process cwd is the global config directory, not the workspace");
}

void test_lsp_configured_provider_inspection_does_not_launch_servers()
{
  auto const workspace = make_lsp_workspace("lsp-config-inspection");
  std::filesystem::create_directories(workspace / ".ava");
  auto const marker_path = workspace / "lsp-inspection-marker.txt";
  auto const config_path = workspace / ".ava" / "lsp.json";
  std::ofstream config(config_path, std::ios::binary | std::ios::trunc);
  config << "{\"version\":1,\"servers\":[{\"id\":\"fake\",\"argv\":[\"" << ava::core::json::escape(AVA_FAKE_LSP_SERVER_PATH) << "\",\"--cwd-marker\",\""
         << ava::core::json::escape(marker_path.generic_string()) << "\"],\"file_extensions\":[\".cpp\"]}]}";
  config.close();

  auto inspection = ava::lsp::inspect_configured_lsp_provider(ava::lsp::ConfiguredLspProviderFiles{
      .global_config_file = {},
      .project_config_file = config_path,
      .workspace_root = workspace,
  });
  expect(inspection.configs.size() == 1 && inspection.error_count == 0 && inspection.server_count == 1 && inspection.configs.front().loaded &&
             inspection.configs.front().server_count == 1,
         "configured LSP inspection parses valid config metadata");
  expect(!std::filesystem::exists(marker_path), "configured LSP inspection does not launch configured servers");
}

void test_lsp_configured_provider_rejects_invalid_config()
{
  auto const workspace = make_lsp_workspace("lsp-invalid-config");
  std::filesystem::create_directories(workspace / ".ava");
  std::ofstream config(workspace / ".ava" / "lsp.json", std::ios::binary | std::ios::trunc);
  config << "{\"version\":1,\"servers\":[{\"id\":\"bad id\",\"argv\":[\"server\"]}]}";
  config.close();

  auto provider = ava::lsp::make_configured_lsp_provider(ava::lsp::ConfiguredLspProviderFiles{
      .global_config_file = workspace / "missing-global-lsp.json",
      .project_config_file = workspace / ".ava" / "lsp.json",
      .workspace_root = workspace,
  });
  expect(!provider && provider.error().message().find("id") != std::string::npos,
         !provider ? "configured LSP provider rejects invalid server ids before exposing tools: " + provider.error().format()
                   : "configured LSP provider rejects invalid server ids before exposing tools");

  auto const strict_workspace = make_lsp_workspace("lsp-strict-config");
  std::filesystem::create_directories(strict_workspace / ".ava");
  auto strict_config_path = strict_workspace / ".ava" / "lsp.json";
  auto rejects_config = [&](std::string_view content, std::string_view expected) {
    std::ofstream strict_config(strict_config_path, std::ios::binary | std::ios::trunc);
    strict_config << content;
    strict_config.close();
    auto strict_provider = ava::lsp::make_configured_lsp_provider(ava::lsp::ConfiguredLspProviderFiles{
        .global_config_file = strict_workspace / "missing-global-lsp.json",
        .project_config_file = strict_config_path,
        .workspace_root = strict_workspace,
    });
    expect(!strict_provider && strict_provider.error().message().find(expected) != std::string::npos,
           !strict_provider ? "configured LSP provider rejects malformed typed config fields: " + strict_provider.error().format()
                            : "configured LSP provider rejects malformed typed config fields");
  };
  rejects_config("{\"version\":1.5,\"servers\":[]}", "version");
  rejects_config("{\"version\":1,\"servers\":[123]}", "servers");
  rejects_config("{\"version\":1,\"servers\":[{\"id\":\"fake\",\"argv\":[\"server\",123]}]}", "argv");
  rejects_config("{\"version\":1,\"servers\":[{\"id\":\"fake\",\"argv\":[\"server\"],\"file_extensions\":[\".cpp\",123]}]}", "file_extensions");
  rejects_config("{\"version\":1,\"servers\":[{\"id\":\"fake\",\"argv\":[\"server\"],\"language_id\":123}]}", "language_id");
  rejects_config("{\"version\":1,\"servers\":[{\"id\":\"fake\",\"argv\":[\"server\"],\"timeout_ms\":1000.5}]}", "timeout_ms");
  rejects_config("{\"version\":1,\"servers\":[{\"id\":\"fake\",\"argv\":[\"server\"]},{\"id\":\"fake\",\"argv\":[\"other\"]}]}", "duplicated");

  auto const global_workspace = make_lsp_workspace("lsp-global-relative-config");
  auto const global_config_path = global_workspace / "global-lsp.json";
  {
    std::ofstream global_config(global_config_path, std::ios::binary | std::ios::trunc);
    global_config << "{\"version\":1,\"servers\":[{\"id\":\"fake\",\"argv\":[\"node\",\".ava/lsp-server.js\"]}]}";
  }
  auto global_relative = ava::lsp::make_configured_lsp_provider(ava::lsp::ConfiguredLspProviderFiles{
      .global_config_file = global_config_path,
      .project_config_file = global_workspace / "missing-project-lsp.json",
      .workspace_root = global_workspace,
  });
  expect(!global_relative && global_relative.error().message().find("workspace-relative") != std::string::npos,
         !global_relative ? "configured LSP provider rejects workspace-relative argv in global config: " + global_relative.error().format()
                          : "configured LSP provider rejects workspace-relative argv in global config");

  auto const project_relative_workspace = make_lsp_workspace("lsp-project-relative-config");
  std::filesystem::create_directories(project_relative_workspace / ".ava");
  auto const project_relative_path = project_relative_workspace / ".ava" / "lsp.json";
  {
    std::ofstream project_config(project_relative_path, std::ios::binary | std::ios::trunc);
    project_config << "{\"version\":1,\"servers\":[{\"id\":\"fake\",\"argv\":[\"node\",\".ava/lsp-server.js\"]}]}";
  }
  auto project_relative = ava::lsp::make_configured_lsp_provider(ava::lsp::ConfiguredLspProviderFiles{
      .global_config_file = project_relative_workspace / "missing-global-lsp.json",
      .project_config_file = project_relative_path,
      .workspace_root = project_relative_workspace,
  });
  expect(project_relative.has_value(),
         project_relative ? "configured LSP provider allows workspace-relative argv in trusted project config"
                          : "configured LSP provider allows workspace-relative argv in trusted project config: " + project_relative.error().format());
}

}  // namespace

void run_lsp_tests()
{
  test_lsp_manager_fake_server_diagnostics();
  test_lsp_manager_fake_server_symbols_and_definition();
  test_lsp_manager_malformed_symbols_error();
  test_lsp_manager_malformed_response_error();
  test_lsp_manager_crash_error();
  test_lsp_manager_timeout_error();
  test_lsp_manager_cancellation();
  test_lsp_manager_huge_response_caps();
  test_lsp_diagnostics_tool_and_dispatcher_json();
  test_lsp_file_uri_escapes_encoded_separators();
  test_lsp_dispatcher_redacts_server_error_context();
  test_lsp_dispatcher_bounds_provider_json();
  test_lsp_configured_provider_loads_project_config_lazily();
  test_lsp_configured_provider_loads_global_config_from_safe_cwd();
  test_lsp_configured_provider_inspection_does_not_launch_servers();
  test_lsp_configured_provider_rejects_invalid_config();
}
