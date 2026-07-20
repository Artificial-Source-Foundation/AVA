#include "sys.h"
#include "tests/support/test_harness.h"
#include "ava/agent/tool_dispatcher.h"
#include "ava/tools/file_tools.h"
#include "ava/tools/lsp_tools.h"
#include "ava/lsp/bounded_file_reader.h"
#include "ava/lsp/configured_provider.h"
#include "ava/lsp/lsp_client.h"
#include "ava/core/json.h"
#include "ava/core/path.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "debug.h"

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
  auto const root = create_empty_root(name);

  auto const workspace = root / "workspace";
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

std::optional<std::string> marker_environment_value(std::filesystem::path const& path, std::string_view name)
{
  std::istringstream lines(read_text_file_for_test(path));
  std::string line;
  auto const prefix = std::string(name) + "=";
  while (std::getline(lines, line))
  {
    if (line.starts_with(prefix))
      return line.substr(prefix.size());
  }
  return std::nullopt;
}

class ScopedEnvironmentForTest final
{
 public:
  bool set(std::string name, std::string value)
  {
    if (std::ranges::find_if(saved_, [&](auto const& item) { return item.first == name; }) == saved_.end())
    {
      auto const* existing = std::getenv(name.c_str());
      saved_.emplace_back(name, existing == nullptr ? std::nullopt : std::optional<std::string>(existing));
    }
    return ::setenv(name.c_str(), value.c_str(), 1) == 0;
  }

  ~ScopedEnvironmentForTest()
  {
    for (auto const& [name, value] : saved_)
    {
      if (value)
        static_cast<void>(::setenv(name.c_str(), value->c_str(), 1));
      else
        static_cast<void>(::unsetenv(name.c_str()));
    }
  }

 private:
  std::vector<std::pair<std::string, std::optional<std::string>>> saved_;
};

std::optional<pid_t> read_pid_file_for_test(std::filesystem::path const& path)
{
  std::ifstream file(path, std::ios::binary);
  long long value = 0;
  file >> value;
  if (!file || value <= 0)
    return std::nullopt;
  return static_cast<pid_t>(value);
}

struct ProcessMarker
{
  pid_t pid = -1;
  pid_t pgid = -1;
};

std::optional<ProcessMarker> read_process_marker_for_test(std::filesystem::path const& path)
{
  std::ifstream file(path, std::ios::binary);
  long long pid = 0;
  long long pgid = 0;
  file >> pid >> pgid;
  if (!file || pid <= 1 || pgid <= 1)
    return std::nullopt;
  return ProcessMarker{.pid = static_cast<pid_t>(pid), .pgid = static_cast<pid_t>(pgid)};
}

std::optional<ProcessMarker> wait_for_process_marker_for_test(std::filesystem::path const& path)
{
  for (int index = 0; index < 100; ++index)
  {
    if (auto marker = read_process_marker_for_test(path))
      return marker;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return read_process_marker_for_test(path);
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

class TestOwnedProcessGroupCleanup final
{
 public:
  void arm(pid_t pgid)
  {
    if (pgid > 1 && pgid != getpgrp())
      pgid_ = pgid;
  }

  ~TestOwnedProcessGroupCleanup()
  {
    if (pgid_ <= 1 || pgid_ == getpgrp())
      return;
    kill(-pgid_, SIGKILL);
    wait_for_process_group_exit(pgid_);
  }

 private:
  pid_t pgid_ = -1;
};

bool file_is_not_created(std::filesystem::path const& path)
{
  for (int index = 0; index < 20; ++index)
  {
    if (std::filesystem::exists(path))
      return false;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return true;
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

class ContextualLspFailureProvider final : public ava::lsp::DiagnosticsProvider
{
 public:
  [[nodiscard]] ava::core::Result<std::vector<ava::lsp::Diagnostic>> diagnostics(std::filesystem::path const&, ava::lsp::CancelCallback = nullptr) override
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "synthetic LSP failure");
    error.with_context("timeout_ms", "250");
    error.with_context("phase", "request");
    error.with_context("method", "textDocument/diagnostic");
    error.with_context("command", "sensitive-command");
    error.with_context("workspace", "/sensitive-workspace");
    error.with_context("path", "/sensitive-path");
    error.with_context("cause", "sensitive-cause");
    error.with_context("status", "exit 23");
    error.with_context("timeout_ms", "not-a-timeout");
    error.with_context("phase", "unrecognized-phase");
    error.with_context("method", "unrecognized/method");
    return std::unexpected(std::move(error));
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
  bool document_symbols_success = document_symbols && document_symbols->size() == 2 && (*document_symbols)[0].name == "main" &&
    (*document_symbols)[0].path == workspace / "main.cpp" && (*document_symbols)[1].container == "main";
  expect(document_symbols_success, "LSP manager requests and parses document symbols");
#ifdef CWDEBUG
  if (!document_symbols_success)
  {
    if (document_symbols)
      Dout(dc::warning, "document_symbols = " << *document_symbols);
    else
      Dout(dc::warning, "document_symbols returned error: " << document_symbols.error());
  }
#endif

  auto workspace_symbols = (*client)->workspace_symbols("main");
  bool workspace_symbols_success = workspace_symbols && workspace_symbols->size() == 1 && (*workspace_symbols)[0].name == "main" &&
    (*workspace_symbols)[0].container == "global" && (*workspace_symbols)[0].path == workspace / "main.cpp";
  expect(workspace_symbols_success, "LSP manager requests and parses workspace symbols");
#ifdef CWDEBUG
  if (!workspace_symbols_success)
  {
    if (workspace_symbols)
      Dout(dc::warning, "workspace_symbols = " << *workspace_symbols);
    else
      Dout(dc::warning, "workspace_symbols returned error: " << workspace_symbols.error());
  }
#endif

  auto definitions = (*client)->definitions(workspace / "main.cpp", 0, 4);
  bool definitions_success = definitions && definitions->size() == 1 && (*definitions)[0].path == workspace / "main.cpp" &&
    (*definitions)[0].range.start_line == 0 && (*definitions)[0].range.start_column == 4;
  expect(definitions_success, "LSP manager requests and parses definitions");
#ifdef CWDEBUG
  if (!definitions_success)
  {
    if (definitions)
      Dout(dc::warning, "definitions = " << *definitions);
    else
      Dout(dc::warning, "definitions returned error: " << definitions.error());
  }
#endif

  auto references = (*client)->references(workspace / "main.cpp", 0, 4);
  bool references_success = references && references->size() == 2 && (*references)[0].path == workspace / "main.cpp" && (*references)[1].range.start_column == 13;
  expect(references_success, "LSP manager sends didOpen before references and parses locations");
#ifdef CWDEBUG
  if (!references_success)
  {
    if (references)
      Dout(dc::warning, "references = " << *references);
    else
      Dout(dc::warning, "references returned error: " << references.error());
  }
#endif
}

void test_lsp_manager_rejects_ancestor_symlinks()
{
  auto const workspace = make_lsp_workspace("lsp-ancestor-symlink-document");
  auto const real_directory = workspace / "real";
  std::filesystem::create_directories(real_directory);
  std::ofstream source(real_directory / "linked.cpp", std::ios::binary | std::ios::trunc);
  source << "int linked() { return 0; }\n";
  source.close();
  std::filesystem::create_directory_symlink(real_directory, workspace / "linked");

  auto client = ava::lsp::SubprocessLspClient::start(ava::lsp::ServerConfig{
      .argv = fake_lsp_argv(),
      .workspace_root = workspace,
      .process_cwd = workspace,
      .request_timeout = std::chrono::milliseconds(1000),
  });
  auto definitions =
      client ? (*client)->definitions(workspace / "linked" / "linked.cpp", 0, 4)
             : ava::core::Result<std::vector<ava::lsp::Location>>{std::unexpected(ava::core::Error(ava::core::ErrorCategory::Tool, "missing client"))};
  expect(!definitions && definitions.error().format().find("symlink") != std::string::npos,
         "LSP didOpen rejects a workspace document reached through an ancestor symlink");

  auto const config_workspace = make_lsp_workspace("lsp-ancestor-symlink-config");
  auto const real_config_directory = config_workspace / "real-ava";
  std::filesystem::create_directories(real_config_directory);
  std::ofstream config(real_config_directory / "lsp.json", std::ios::binary | std::ios::trunc);
  config << "{\"version\":1,\"servers\":[{\"id\":\"fake\",\"argv\":[\"" << ava::core::json::escape(AVA_FAKE_LSP_SERVER_PATH) << "\"]}]}";
  config.close();
  std::filesystem::create_directory_symlink(real_config_directory, config_workspace / ".ava");
  auto provider = ava::lsp::make_configured_lsp_provider(ava::lsp::ConfiguredLspProviderFiles{
      .global_config_file = config_workspace / "missing-global-lsp.json",
      .project_config_file = config_workspace / ".ava" / "lsp.json",
      .workspace_root = config_workspace,
  });
  expect(!provider && provider.error().format().find("symlink") != std::string::npos,
         "LSP project config rejects an ancestor symlink beneath the workspace anchor");
}

void test_lsp_bounded_reader_snapshot_and_openat2_fallback()
{
  auto const workspace = make_lsp_workspace("lsp-bounded-reader");
  auto const snapshot_path = workspace / "snapshot.cpp";
  auto const moved_path = workspace / "snapshot-moved.cpp";
  std::ofstream snapshot_file(snapshot_path, std::ios::binary | std::ios::trunc);
  snapshot_file << "descriptor snapshot\n";
  snapshot_file.close();
  auto snapshot = ava::lsp::read_bounded_lsp_file(ava::lsp::BoundedFileReadOptions{
      .path = snapshot_path,
      .workspace_root = workspace,
      .max_bytes = 1024,
      .scope = ava::lsp::BoundedFileReadScope::Workspace,
      .missing_ok = false,
      .deadline = std::chrono::steady_clock::time_point::max(),
      .cancel_requested = nullptr,
      .open_strategy = ava::lsp::BoundedFileOpenStrategy::Automatic,
      .after_open_for_testing =
          [&] {
            std::filesystem::rename(snapshot_path, moved_path);
            std::filesystem::create_symlink(moved_path, snapshot_path);
          },
  });
  expect(snapshot && *snapshot && **snapshot == "descriptor snapshot\n",
         "LSP bounded reader keeps reading the opened regular-file descriptor when its pathname is swapped");

  auto const fallback_path = workspace / "fallback.cpp";
  std::ofstream fallback_file(fallback_path, std::ios::binary | std::ios::trunc);
  fallback_file << "component fallback\n";
  fallback_file.close();
  auto fallback = ava::lsp::read_bounded_lsp_file(ava::lsp::BoundedFileReadOptions{
      .path = fallback_path,
      .workspace_root = workspace,
      .max_bytes = 1024,
      .scope = ava::lsp::BoundedFileReadScope::Workspace,
      .missing_ok = false,
      .deadline = std::chrono::steady_clock::time_point::max(),
      .cancel_requested = nullptr,
      .open_strategy = ava::lsp::BoundedFileOpenStrategy::ForceComponentFallback,
      .after_open_for_testing = nullptr,
  });
  expect(fallback && *fallback && **fallback == "component fallback\n", "LSP descriptor-by-component fallback reads a normal workspace file without openat2");

  auto fallback_symlink = ava::lsp::read_bounded_lsp_file(ava::lsp::BoundedFileReadOptions{
      .path = snapshot_path,
      .workspace_root = workspace,
      .max_bytes = 1024,
      .scope = ava::lsp::BoundedFileReadScope::Workspace,
      .missing_ok = false,
      .deadline = std::chrono::steady_clock::time_point::max(),
      .cancel_requested = nullptr,
      .open_strategy = ava::lsp::BoundedFileOpenStrategy::ForceComponentFallback,
      .after_open_for_testing = nullptr,
  });
  expect(!fallback_symlink && fallback_symlink.error().format().find("symlink") != std::string::npos,
         "LSP descriptor-by-component fallback rejects final symlinks without openat2");
}

void test_lsp_manager_rejects_fifo_symlink_and_oversize_documents()
{
  auto const workspace = make_lsp_workspace("lsp-unsafe-documents");
  auto client = ava::lsp::SubprocessLspClient::start(ava::lsp::ServerConfig{
      .argv = fake_lsp_argv(),
      .workspace_root = workspace,
      .process_cwd = workspace,
      .request_timeout = std::chrono::milliseconds(1000),
  });
  expect(client.has_value(), "LSP unsafe-document test starts fake server");
  if (!client)
    return;

  auto const fifo_path = workspace / "document.fifo";
  bool const fifo_created = ::mkfifo(fifo_path.c_str(), S_IRUSR | S_IWUSR) == 0;
  auto fifo =
      fifo_created
          ? (*client)->definitions(fifo_path, 0, 0)
          : ava::core::Result<std::vector<ava::lsp::Location>>{std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to create FIFO"))};
  expect(fifo_created && !fifo && fifo.error().format().find("regular file") != std::string::npos,
         "LSP didOpen rejects a document FIFO promptly without opening it as a blocking stream");
  std::filesystem::remove(fifo_path);

  auto const final_symlink = workspace / "document-link.cpp";
  std::filesystem::create_symlink(workspace / "main.cpp", final_symlink);
  auto linked = (*client)->definitions(final_symlink, 0, 0);
  expect(!linked && linked.error().format().find("symlink") != std::string::npos,
         "LSP didOpen rejects a final document symlink through its final descriptor open");

  auto const oversize_path = workspace / "oversize.cpp";
  std::ofstream oversize(oversize_path, std::ios::binary | std::ios::trunc);
  oversize << std::string(512U * 1024U + 1U, 'x');
  oversize.close();
  auto oversized = (*client)->definitions(oversize_path, 0, 0);
  expect(!oversized && oversized.error().format().find("exceeds maximum size") != std::string::npos,
         "LSP didOpen enforces its document size cap from the opened descriptor before allocation");

  auto const canceled_fifo = workspace / "canceled-document.fifo";
  bool const canceled_fifo_created = ::mkfifo(canceled_fifo.c_str(), S_IRUSR | S_IWUSR) == 0;
  int cancel_checks = 0;
  auto canceled =
      canceled_fifo_created
          ? (*client)->definitions(canceled_fifo, 0, 0, [&] { return ++cancel_checks >= 5; })
          : ava::core::Result<std::vector<ava::lsp::Location>>{std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to create FIFO"))};
  expect(canceled_fifo_created && !canceled && canceled.error().message().find("canceled") != std::string::npos && !(*client)->is_alive(),
         "LSP cancellation observed after document fstat wins over FIFO type classification and tears down the child");
}

void test_lsp_configured_provider_rejects_unsafe_config_files()
{
  auto const workspace = make_lsp_workspace("lsp-unsafe-configs");
  auto const fifo_path = workspace / "global.fifo";
  bool const fifo_created = ::mkfifo(fifo_path.c_str(), S_IRUSR | S_IWUSR) == 0;
  auto fifo_provider = ava::lsp::make_configured_lsp_provider(ava::lsp::ConfiguredLspProviderFiles{
      .global_config_file = fifo_path,
      .project_config_file = {},
      .workspace_root = workspace,
  });
  expect(fifo_created && !fifo_provider && fifo_provider.error().format().find("regular file") != std::string::npos,
         "configured LSP provider rejects a global config FIFO promptly through the shared descriptor reader");
  std::filesystem::remove(fifo_path);

  auto const global_target = workspace / "global-target.json";
  std::ofstream global_config(global_target, std::ios::binary | std::ios::trunc);
  global_config << "{\"version\":1,\"servers\":[]}";
  global_config.close();
  auto const global_symlink = workspace / "global-link.json";
  std::filesystem::create_symlink(global_target, global_symlink);
  auto global_link_provider = ava::lsp::make_configured_lsp_provider(ava::lsp::ConfiguredLspProviderFiles{
      .global_config_file = global_symlink,
      .project_config_file = {},
      .workspace_root = workspace,
  });
  expect(!global_link_provider && global_link_provider.error().format().find("symlink") != std::string::npos,
         "configured LSP provider rejects a final global config symlink without losing external-path compatibility");

  auto const global_real_directory = workspace / "global-real";
  std::filesystem::create_directories(global_real_directory);
  std::ofstream global_ancestor_config(global_real_directory / "lsp.json", std::ios::binary | std::ios::trunc);
  global_ancestor_config << "{\"version\":1,\"servers\":[]}";
  global_ancestor_config.close();
  auto const global_ancestor = workspace / "global-ancestor";
  Dout(dc::notice, "Calling std::filesystem::create_directory_symlink(" << global_real_directory << ", " << global_ancestor << ")");
  std::filesystem::create_directory_symlink(global_real_directory, global_ancestor);
  auto global_ancestor_provider = ava::lsp::make_configured_lsp_provider(ava::lsp::ConfiguredLspProviderFiles{
      .global_config_file = global_ancestor / "lsp.json",
      .project_config_file = {},
      .workspace_root = workspace,
  });
  bool global_ancestor_provider_success = !global_ancestor_provider && global_ancestor_provider.error().format().find("symlink") != std::string::npos;
  expect(global_ancestor_provider_success, "configured LSP provider rejects an ancestor symlink for an absolute external global config path");
#ifdef CWDEBUG
  if (!global_ancestor_provider_success)
  {
    if (global_ancestor_provider)
      Dout(dc::warning, "global_ancestor_provider = " << *global_ancestor_provider);
    else
      Dout(dc::warning, "global_ancestor_provider returned error: " << global_ancestor_provider.error());
  }
#endif

  std::filesystem::create_directories(workspace / ".ava");
  auto const project_config = workspace / ".ava" / "lsp.json";
  bool const project_fifo_created = ::mkfifo(project_config.c_str(), S_IRUSR | S_IWUSR) == 0;
  auto project_fifo_provider = ava::lsp::make_configured_lsp_provider(ava::lsp::ConfiguredLspProviderFiles{
      .global_config_file = {},
      .project_config_file = project_config,
      .workspace_root = workspace,
  });
  expect(project_fifo_created && !project_fifo_provider && project_fifo_provider.error().format().find("regular file") != std::string::npos,
         "configured LSP provider rejects a project config FIFO beneath the workspace anchor");
  std::filesystem::remove(project_config);

  auto const project_target = workspace / ".ava" / "target.json";
  std::ofstream project_target_file(project_target, std::ios::binary | std::ios::trunc);
  project_target_file << "{\"version\":1,\"servers\":[]}";
  project_target_file.close();
  std::filesystem::create_symlink(project_target, project_config);
  auto project_link_provider = ava::lsp::make_configured_lsp_provider(ava::lsp::ConfiguredLspProviderFiles{
      .global_config_file = {},
      .project_config_file = project_config,
      .workspace_root = workspace,
  });
  expect(!project_link_provider && project_link_provider.error().format().find("symlink") != std::string::npos,
         "configured LSP provider rejects a final project config symlink beneath the workspace anchor");
  std::filesystem::remove(project_config);

  std::ofstream oversized(project_config, std::ios::binary | std::ios::trunc);
  oversized << std::string(64U * 1024U + 1U, 'x');
  oversized.close();
  auto oversized_provider = ava::lsp::make_configured_lsp_provider(ava::lsp::ConfiguredLspProviderFiles{
      .global_config_file = {},
      .project_config_file = project_config,
      .workspace_root = workspace,
  });
  auto inspection = ava::lsp::inspect_configured_lsp_provider(ava::lsp::ConfiguredLspProviderFiles{
      .global_config_file = {},
      .project_config_file = project_config,
      .workspace_root = workspace,
  });
  expect(!oversized_provider && oversized_provider.error().format().find("exceeds maximum size") != std::string::npos && inspection.error_count == 1 &&
             inspection.configs.size() == 1 && inspection.configs.front().error &&
             inspection.configs.front().error->format().find("exceeds maximum size") != std::string::npos,
         "configured LSP inspection and loading share the one-descriptor cap enforcement without metadata prechecks");
}

void test_lsp_manager_definition_reference_share_one_deadline()
{
  auto const workspace = make_lsp_workspace("lsp-single-definition-budget");
  std::ofstream source(workspace / "main.cpp", std::ios::binary | std::ios::trunc);
  source << std::string(480U * 1024U, 'x');
  source.close();
  auto client = ava::lsp::SubprocessLspClient::start(ava::lsp::ServerConfig{
      .argv = fake_lsp_argv({"--slow-did-open-definition"}),
      .workspace_root = workspace,
      .process_cwd = workspace,
      .startup_timeout = std::chrono::milliseconds(1000),
      .request_timeout = std::chrono::milliseconds(300),
  });
  auto definitions =
      client ? (*client)->definitions(workspace / "main.cpp", 0, 0)
             : ava::core::Result<std::vector<ava::lsp::Location>>{std::unexpected(ava::core::Error(ava::core::ErrorCategory::Tool, "missing client"))};
  expect(!definitions && definitions.error().format().find("timed out") != std::string::npos &&
             definitions.error().format().find("timeout_ms: 300") != std::string::npos && client && !(*client)->is_alive(),
         "LSP definition carries one request deadline through slow didOpen acquisition/write and the response instead of granting a second budget");

  auto references_client = ava::lsp::SubprocessLspClient::start(ava::lsp::ServerConfig{
      .argv = fake_lsp_argv({"--slow-did-open-definition"}),
      .workspace_root = workspace,
      .process_cwd = workspace,
      .startup_timeout = std::chrono::milliseconds(1000),
      .request_timeout = std::chrono::milliseconds(300),
  });
  auto references =
      references_client
          ? (*references_client)->references(workspace / "main.cpp", 0, 0)
          : ava::core::Result<std::vector<ava::lsp::Location>>{std::unexpected(ava::core::Error(ava::core::ErrorCategory::Tool, "missing client"))};
  expect(!references && references.error().format().find("timed out") != std::string::npos && references_client && !(*references_client)->is_alive(),
         "LSP references shares the same single didOpen/request deadline and terminates its child when that budget expires");
}

void test_lsp_manager_filters_child_environment()
{
  auto const workspace = make_lsp_workspace("lsp-filtered-environment");
  auto const marker = workspace / "environment.txt";
  ScopedEnvironmentForTest environment;
  bool const configured = environment.set("HOME", "/tmp/ava-lsp-home") && environment.set("LANG", "C") && environment.set("OPENAI_API_KEY", "lsp-secret") &&
                          environment.set("AVA_LSP_TEST_SECRET", "ava-secret") && environment.set("AVA_UNRELATED", "not-for-lsp");
  auto client = ava::lsp::SubprocessLspClient::start(ava::lsp::ServerConfig{
      .argv = fake_lsp_argv({"--environment-marker", marker.generic_string()}),
      .workspace_root = workspace,
      .process_cwd = workspace,
      .request_timeout = std::chrono::milliseconds(1000),
  });
  expect(configured && client && marker_environment_value(marker, "HOME") == std::optional<std::string>("/tmp/ava-lsp-home") &&
             marker_environment_value(marker, "LANG") == std::optional<std::string>("C") &&
             marker_environment_value(marker, "OPENAI_API_KEY") == std::optional<std::string>("<unset>") &&
             marker_environment_value(marker, "AVA_LSP_TEST_SECRET") == std::optional<std::string>("<unset>") &&
             marker_environment_value(marker, "AVA_UNRELATED") == std::optional<std::string>("<unset>"),
         "LSP child retains HOME and locale compatibility while dropping provider credentials and arbitrary AVA variables");
}

void test_lsp_manager_closed_standard_fds()
{
  auto const workspace = make_lsp_workspace("lsp-closed-standard-fds");
  pid_t const test_child = fork();
  if (test_child == 0)
  {
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    auto client = ava::lsp::SubprocessLspClient::start(ava::lsp::ServerConfig{
        .argv = fake_lsp_argv(),
        .workspace_root = workspace,
        .process_cwd = workspace,
        .request_timeout = std::chrono::milliseconds(3000),
    });
    if (!client)
      _exit(1);
    auto diagnostics = (*client)->diagnostics(workspace / "main.cpp");
    _exit(diagnostics && diagnostics->size() == 1 && (*diagnostics)[0].message == "fake diagnostic from LSP" ? 0 : 1);
  }
  if (test_child < 0)
  {
    expect(false, "LSP closed-standard-fds test forks an isolated helper");
    return;
  }

  int status = 0;
  pid_t waited = -1;
  do
  {
    waited = waitpid(test_child, &status, 0);
  } while (waited < 0 && errno == EINTR);
  expect(waited == test_child && WIFEXITED(status) && WEXITSTATUS(status) == 0,
         "LSP initializes and requests diagnostics when an isolated helper starts with fds 0, 1, and 2 closed");
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

void test_lsp_manager_delayed_initialize_uses_startup_timeout()
{
  auto const workspace = make_lsp_workspace("lsp-delayed-initialize");
  auto client = ava::lsp::SubprocessLspClient::start(ava::lsp::ServerConfig{
      .argv = fake_lsp_argv({"--delayed-initialize"}),
      .workspace_root = workspace,
      .process_cwd = workspace,
      .startup_timeout = std::chrono::milliseconds(1000),
      .request_timeout = std::chrono::milliseconds(200),
  });
  expect(client.has_value(), "LSP startup accepts a delayed initialize response within the independent startup timeout");
  auto diagnostics =
      client ? (*client)->diagnostics(workspace / "main.cpp")
             : ava::core::Result<std::vector<ava::lsp::Diagnostic>>{std::unexpected(ava::core::Error(ava::core::ErrorCategory::Tool, "missing client"))};
  expect(diagnostics && diagnostics->size() == 1 && (*diagnostics)[0].message == "fake diagnostic from LSP",
         "LSP startup sends initialized before diagnostics after delayed initialization");
}

void test_lsp_manager_timeout_error()
{
  auto const workspace = make_lsp_workspace("lsp-timeout");
  auto client = ava::lsp::SubprocessLspClient::start(ava::lsp::ServerConfig{
      .argv = fake_lsp_argv({"--sleep-diagnostics"}),
      .workspace_root = workspace,
      .process_cwd = workspace,
      .startup_timeout = std::chrono::milliseconds(1000),
      .request_timeout = std::chrono::milliseconds(250),
  });
  expect(client.has_value(), "LSP timeout test starts sleeping fake server");
  auto diagnostics =
      client ? (*client)->diagnostics(workspace / "main.cpp")
             : ava::core::Result<std::vector<ava::lsp::Diagnostic>>{std::unexpected(ava::core::Error(ava::core::ErrorCategory::Tool, "missing client"))};
  auto const detail = diagnostics ? std::string{} : diagnostics.error().format();
  expect(!diagnostics && detail.find("timed out") != std::string::npos && detail.find("timeout_ms: 250") != std::string::npos &&
             detail.find("phase: request") != std::string::npos && detail.find("method: textDocument/diagnostic") != std::string::npos,
         "LSP diagnostics timeout keeps the request budget and identifies its request phase and method");
}

void test_lsp_manager_startup_timeout_and_validation()
{
  ava::lsp::ServerConfig const direct_defaults;
  expect(direct_defaults.startup_timeout == std::chrono::milliseconds(10000) && direct_defaults.request_timeout == std::chrono::milliseconds(3000),
         "direct LSP ServerConfig callers retain independent startup and request defaults");

  auto const timeout_workspace = make_lsp_workspace("lsp-startup-timeout");
  auto const timeout_pgid_file = timeout_workspace / "lsp-startup-timeout-pgid.txt";
  auto timed_out = ava::lsp::SubprocessLspClient::start(ava::lsp::ServerConfig{
      .argv = fake_lsp_argv({"--sleep-initialize-marker", timeout_pgid_file.generic_string()}),
      .workspace_root = timeout_workspace,
      .process_cwd = timeout_workspace,
      .startup_timeout = std::chrono::milliseconds(200),
      .request_timeout = std::chrono::milliseconds(1000),
  });
  auto const timeout_pgid = read_pid_file_for_test(timeout_pgid_file);
  auto const timeout_detail = timed_out ? std::string{} : timed_out.error().format();
  expect(!timed_out && timeout_detail.find("timed out") != std::string::npos && timeout_detail.find("timeout_ms: 200") != std::string::npos &&
             timeout_detail.find("phase: startup") != std::string::npos && timeout_detail.find("method: initialize") != std::string::npos && timeout_pgid &&
             wait_for_process_group_exit(*timeout_pgid),
         "LSP startup timeout reports initialize startup context and terminates the server process group");

  auto const validation_workspace = make_lsp_workspace("lsp-timeout-validation");
  auto const startup_marker = validation_workspace / "invalid-startup-launched.txt";
  auto invalid_startup = ava::lsp::SubprocessLspClient::start(ava::lsp::ServerConfig{
      .argv = fake_lsp_argv({"--launch-marker", startup_marker.generic_string()}),
      .workspace_root = validation_workspace,
      .process_cwd = validation_workspace,
      .startup_timeout = std::chrono::milliseconds(99),
      .request_timeout = std::chrono::milliseconds(200),
  });
  auto const startup_detail = invalid_startup ? std::string{} : invalid_startup.error().format();
  expect(!invalid_startup && startup_detail.find("startup timeout") != std::string::npos &&
             startup_detail.find("field: startup_timeout") != std::string::npos && file_is_not_created(startup_marker),
         "LSP rejects invalid startup timeout bounds before launching a server");

  auto const request_marker = validation_workspace / "invalid-request-launched.txt";
  auto invalid_request = ava::lsp::SubprocessLspClient::start(ava::lsp::ServerConfig{
      .argv = fake_lsp_argv({"--launch-marker", request_marker.generic_string()}),
      .workspace_root = validation_workspace,
      .process_cwd = validation_workspace,
      .startup_timeout = std::chrono::milliseconds(200),
      .request_timeout = std::chrono::milliseconds(30001),
  });
  auto const request_detail = invalid_request ? std::string{} : invalid_request.error().format();
  expect(!invalid_request && request_detail.find("request timeout") != std::string::npos &&
             request_detail.find("field: request_timeout") != std::string::npos && file_is_not_created(request_marker),
         "LSP rejects invalid request timeout bounds before launching a server");
}

void test_lsp_manager_containment_cleanup()
{
  auto const timeout_workspace = make_lsp_workspace("lsp-term-ignoring-descendant-timeout");
  auto const timeout_leader_marker = timeout_workspace / "leader-marker.txt";
  auto const timeout_descendant_marker = timeout_workspace / "descendant-marker.txt";
  TestOwnedProcessGroupCleanup timeout_cleanup;
  auto timeout_client = ava::lsp::SubprocessLspClient::start(ava::lsp::ServerConfig{
      .argv =
          fake_lsp_argv({"--term-ignoring-descendant-diagnostics-markers", timeout_leader_marker.generic_string(), timeout_descendant_marker.generic_string()}),
      .workspace_root = timeout_workspace,
      .process_cwd = timeout_workspace,
      .startup_timeout = std::chrono::milliseconds(1000),
      .request_timeout = std::chrono::milliseconds(250),
  });
  auto timeout_diagnostics =
      timeout_client
          ? (*timeout_client)->diagnostics(timeout_workspace / "main.cpp")
          : ava::core::Result<std::vector<ava::lsp::Diagnostic>>{std::unexpected(ava::core::Error(ava::core::ErrorCategory::Tool, "missing client"))};
  auto const timeout_leader = wait_for_process_marker_for_test(timeout_leader_marker);
  auto const timeout_descendant = wait_for_process_marker_for_test(timeout_descendant_marker);
  if (timeout_leader)
    timeout_cleanup.arm(timeout_leader->pgid);
  auto const timeout_detail = timeout_diagnostics ? std::string{} : timeout_diagnostics.error().format();
  bool const timeout_group_cleaned = timeout_leader && wait_for_process_group_exit(timeout_leader->pgid);
  expect(!timeout_diagnostics && timeout_detail.find("timed out") != std::string::npos && timeout_leader && timeout_descendant &&
             timeout_leader->pid == timeout_leader->pgid && timeout_leader->pgid != getpgrp() && timeout_descendant->pid != timeout_leader->pid &&
             timeout_descendant->pgid == timeout_leader->pgid && timeout_group_cleaned,
         "LSP timeout TERM/KILL teardown removes an in-group TERM-ignoring descendant without touching the runner group");

  auto const exit_workspace = make_lsp_workspace("lsp-leader-exits-first");
  auto const exit_leader_marker = exit_workspace / "leader-marker.txt";
  auto const exit_descendant_marker = exit_workspace / "descendant-marker.txt";
  TestOwnedProcessGroupCleanup exit_cleanup;
  auto exit_client = ava::lsp::SubprocessLspClient::start(ava::lsp::ServerConfig{
      .argv = fake_lsp_argv({"--leader-exits-first-diagnostics-markers", exit_leader_marker.generic_string(), exit_descendant_marker.generic_string()}),
      .workspace_root = exit_workspace,
      .process_cwd = exit_workspace,
      .startup_timeout = std::chrono::milliseconds(1000),
      .request_timeout = std::chrono::milliseconds(1000),
  });
  auto exit_diagnostics =
      exit_client ? (*exit_client)->diagnostics(exit_workspace / "main.cpp")
                  : ava::core::Result<std::vector<ava::lsp::Diagnostic>>{std::unexpected(ava::core::Error(ava::core::ErrorCategory::Tool, "missing client"))};
  auto const exit_leader = wait_for_process_marker_for_test(exit_leader_marker);
  auto const exit_descendant = wait_for_process_marker_for_test(exit_descendant_marker);
  if (exit_leader)
    exit_cleanup.arm(exit_leader->pgid);
  auto const exit_detail = exit_diagnostics ? std::string{} : exit_diagnostics.error().format();
  bool const exit_group_cleaned = exit_leader && wait_for_process_group_exit(exit_leader->pgid);
  expect(!exit_diagnostics && exit_detail.find("LSP server exited") != std::string::npos && exit_detail.find("status: exit 23") != std::string::npos &&
             exit_leader && exit_descendant && exit_leader->pid == exit_leader->pgid && exit_leader->pgid != getpgrp() &&
             exit_descendant->pid != exit_leader->pid && exit_descendant->pgid == exit_leader->pgid && exit_group_cleaned,
         "LSP leader exit is inspected without reaping, then tears down its verified group before returning status");
}

void test_lsp_manager_cancellation_precedence()
{
  auto const timeout_workspace = make_lsp_workspace("lsp-cancel-vs-timeout");
  auto const timeout_marker = timeout_workspace / "timeout-marker.txt";
  auto timeout_client = ava::lsp::SubprocessLspClient::start(ava::lsp::ServerConfig{
      .argv = fake_lsp_argv({"--sleep-diagnostics-marker", timeout_marker.generic_string()}),
      .workspace_root = timeout_workspace,
      .process_cwd = timeout_workspace,
      .startup_timeout = std::chrono::milliseconds(1000),
      .request_timeout = std::chrono::milliseconds(500),
  });
  TestOwnedProcessGroupCleanup timeout_cleanup;
  std::optional<std::chrono::steady_clock::time_point> timeout_cancel_seen;
  auto timeout_cancel = [&] {
    if (!read_pid_file_for_test(timeout_marker))
      return false;
    auto const now = std::chrono::steady_clock::now();
    if (!timeout_cancel_seen)
    {
      timeout_cancel_seen = now;
      return false;
    }
    return now - *timeout_cancel_seen >= std::chrono::milliseconds(150);
  };
  ava::tools::ToolContext const timeout_context{
      .workspace_dir = timeout_workspace,
      .cancel_requested = timeout_cancel,
      .lsp_diagnostics_provider = timeout_client ? std::shared_ptr<ava::lsp::DiagnosticsProvider>(*timeout_client) : nullptr};
  ava::agent::ToolDispatcher const timeout_dispatcher(timeout_context);
  auto timeout_dispatched = timeout_dispatcher.dispatch(
      ava::agent::ProviderToolCall{.id = "call_cancel_timeout", .name = "lsp_diagnostics", .arguments_json = "{\"path\":\"main.cpp\"}"});
  auto const timeout_process = wait_for_process_marker_for_test(timeout_marker);
  if (timeout_process)
    timeout_cleanup.arm(timeout_process->pgid);
  bool const timeout_group_cleaned = timeout_process && wait_for_process_group_exit(timeout_process->pgid);
  expect(timeout_dispatched && !timeout_dispatched->success && timeout_dispatched->payload.status == ava::agent::ToolResultStatus::Canceled &&
             timeout_dispatched->result_text.find("LSP query canceled") != std::string::npos && timeout_process && timeout_group_cleaned,
         "LSP cancellation after a poll wins over request timeout and preserves canceled ToolResult status");

  auto const exit_workspace = make_lsp_workspace("lsp-cancel-vs-exit");
  auto const exit_leader_marker = exit_workspace / "leader-marker.txt";
  auto const exit_descendant_marker = exit_workspace / "descendant-marker.txt";
  auto exit_client = ava::lsp::SubprocessLspClient::start(ava::lsp::ServerConfig{
      .argv = fake_lsp_argv({"--leader-exits-after-marker-diagnostics-markers", exit_leader_marker.generic_string(), exit_descendant_marker.generic_string()}),
      .workspace_root = exit_workspace,
      .process_cwd = exit_workspace,
      .startup_timeout = std::chrono::milliseconds(1000),
      .request_timeout = std::chrono::milliseconds(1000),
  });
  TestOwnedProcessGroupCleanup exit_cleanup;
  std::optional<std::chrono::steady_clock::time_point> exit_cancel_seen;
  auto exit_cancel = [&] {
    if (!read_pid_file_for_test(exit_leader_marker))
      return false;
    auto const now = std::chrono::steady_clock::now();
    if (!exit_cancel_seen)
    {
      exit_cancel_seen = now;
      return false;
    }
    return now - *exit_cancel_seen >= std::chrono::milliseconds(150);
  };
  auto exit_diagnostics =
      exit_client ? (*exit_client)->diagnostics(exit_workspace / "main.cpp", exit_cancel)
                  : ava::core::Result<std::vector<ava::lsp::Diagnostic>>{std::unexpected(ava::core::Error(ava::core::ErrorCategory::Tool, "missing client"))};
  auto const exit_leader = wait_for_process_marker_for_test(exit_leader_marker);
  auto const exit_descendant = wait_for_process_marker_for_test(exit_descendant_marker);
  if (exit_leader)
    exit_cleanup.arm(exit_leader->pgid);
  bool const exit_group_cleaned = exit_leader && wait_for_process_group_exit(exit_leader->pgid);
  expect(!exit_diagnostics && exit_diagnostics.error().message().find("canceled") != std::string::npos && exit_leader && exit_descendant &&
             exit_descendant->pgid == exit_leader->pgid && exit_group_cleaned,
         "LSP cancellation after a poll wins before an exiting leader's EOF/error classification and tears down its group");
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

void test_lsp_dispatcher_preserves_safe_error_context_only()
{
  auto const workspace = make_lsp_workspace("lsp-safe-error-context");
  ava::tools::ToolContext const context{.workspace_dir = workspace, .lsp_diagnostics_provider = std::make_shared<ContextualLspFailureProvider>()};
  ava::agent::ToolDispatcher const dispatcher(context);
  auto dispatched =
      dispatcher.dispatch(ava::agent::ProviderToolCall{.id = "call_lsp_safe_context", .name = "lsp_diagnostics", .arguments_json = "{\"path\":\"main.cpp\"}"});
  expect(
      dispatched && !dispatched->success && dispatched->result_text.find("LSP query failed") != std::string::npos &&
          dispatched->result_text.find("timeout_ms: 250") != std::string::npos && dispatched->result_text.find("phase: request") != std::string::npos &&
          dispatched->result_text.find("method: textDocument/diagnostic") != std::string::npos &&
          dispatched->result_text.find("sensitive-command") == std::string::npos && dispatched->result_text.find("/sensitive-workspace") == std::string::npos &&
          dispatched->result_text.find("/sensitive-path") == std::string::npos && dispatched->result_text.find("sensitive-cause") == std::string::npos &&
          dispatched->result_text.find("status: exit 23") == std::string::npos && dispatched->result_text.find("not-a-timeout") == std::string::npos &&
          dispatched->result_text.find("unrecognized-phase") == std::string::npos && dispatched->result_text.find("unrecognized/method") == std::string::npos,
      "LSP dispatcher JSON preserves only validated timeout, phase, and method context while redacting local error details");
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
  auto const root = create_empty_root("lsp-global-safe-cwd");

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
  expect(read_text_file_for_test(marker_path) == ava::core::normalized_absolute_path(global_config_dir).string(),
         "configured global LSP server process cwd is the global config directory, not the workspace");
}

void test_lsp_configured_provider_timeout_defaults()
{
  auto const fallback_workspace = make_lsp_workspace("lsp-configured-startup-fallback");
  std::filesystem::create_directories(fallback_workspace / ".ava");
  auto const fallback_config_path = fallback_workspace / ".ava" / "lsp.json";
  {
    std::ofstream config(fallback_config_path, std::ios::binary | std::ios::trunc);
    config << "{\"version\":1,\"servers\":[{\"id\":\"fake\",\"argv\":[\"" << ava::core::json::escape(AVA_FAKE_LSP_SERVER_PATH)
           << "\",\"--delayed-initialize\"],\"timeout_ms\":200}]}";
  }
  auto fallback_provider = ava::lsp::make_configured_lsp_provider(ava::lsp::ConfiguredLspProviderFiles{
      .global_config_file = fallback_workspace / "missing-global-lsp.json",
      .project_config_file = fallback_config_path,
      .workspace_root = fallback_workspace,
      .permission_resolver = [](ava::permissions::PermissionPrompt const&) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
        return ava::permissions::PermissionResolutionDecision{ava::permissions::PermissionResolution::Allow, "test allow"};
      },
  });
  ava::tools::ToolContext const fallback_context{.workspace_dir = fallback_workspace,
                                                 .lsp_diagnostics_provider = fallback_provider ? *fallback_provider : nullptr};
  ava::agent::ToolDispatcher const fallback_dispatcher(fallback_context);
  auto fallback_result = fallback_dispatcher.dispatch(
      ava::agent::ProviderToolCall{.id = "call_lsp_startup_fallback", .name = "lsp_diagnostics", .arguments_json = "{\"path\":\"main.cpp\"}"});
  expect(fallback_provider && fallback_result && !fallback_result->success && fallback_result->result_text.find("timeout_ms: 200") != std::string::npos &&
             fallback_result->result_text.find("phase: startup") != std::string::npos &&
             fallback_result->result_text.find("method: initialize") != std::string::npos,
         "configured LSP startup timeout falls back to parsed timeout_ms and exposes only safe request context to the dispatcher");

  auto const explicit_workspace = make_lsp_workspace("lsp-configured-startup-explicit");
  std::filesystem::create_directories(explicit_workspace / ".ava");
  auto const explicit_config_path = explicit_workspace / ".ava" / "lsp.json";
  {
    std::ofstream config(explicit_config_path, std::ios::binary | std::ios::trunc);
    config << "{\"version\":1,\"servers\":[{\"id\":\"fake\",\"argv\":[\"" << ava::core::json::escape(AVA_FAKE_LSP_SERVER_PATH)
           << "\",\"--delayed-initialize\"],\"timeout_ms\":200,\"startup_timeout_ms\":1000}]}";
  }
  auto explicit_provider = ava::lsp::make_configured_lsp_provider(ava::lsp::ConfiguredLspProviderFiles{
      .global_config_file = explicit_workspace / "missing-global-lsp.json",
      .project_config_file = explicit_config_path,
      .workspace_root = explicit_workspace,
      .permission_resolver = [](ava::permissions::PermissionPrompt const&) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
        return ava::permissions::PermissionResolutionDecision{ava::permissions::PermissionResolution::Allow, "test allow"};
      },
  });
  ava::tools::ToolContext const explicit_context{.workspace_dir = explicit_workspace,
                                                 .lsp_diagnostics_provider = explicit_provider ? *explicit_provider : nullptr};
  ava::agent::ToolDispatcher const explicit_dispatcher(explicit_context);
  auto explicit_result = explicit_dispatcher.dispatch(
      ava::agent::ProviderToolCall{.id = "call_lsp_startup_explicit", .name = "lsp_diagnostics", .arguments_json = "{\"path\":\"main.cpp\"}"});
  expect(explicit_provider && explicit_result && explicit_result->success && explicit_result->result_text.find("fake diagnostic from LSP") != std::string::npos,
         "configured LSP startup_timeout_ms independently extends initialize while timeout_ms remains the request budget");
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
  rejects_config("{\"version\":1,\"servers\":[{\"id\":\"fake\",\"argv\":[\"server\"],\"startup_timeout_ms\":1000.5}]}", "startup_timeout_ms");
  rejects_config("{\"version\":1,\"servers\":[{\"id\":\"fake\",\"argv\":[\"server\"],\"startup_timeout_ms\":99}]}", "startup timeout");
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
  test_lsp_manager_rejects_ancestor_symlinks();
  test_lsp_bounded_reader_snapshot_and_openat2_fallback();
  test_lsp_manager_rejects_fifo_symlink_and_oversize_documents();
  test_lsp_configured_provider_rejects_unsafe_config_files();
  test_lsp_manager_definition_reference_share_one_deadline();
  test_lsp_manager_filters_child_environment();
  test_lsp_manager_closed_standard_fds();
  test_lsp_manager_malformed_symbols_error();
  test_lsp_manager_malformed_response_error();
  test_lsp_manager_crash_error();
  test_lsp_manager_delayed_initialize_uses_startup_timeout();
  test_lsp_manager_timeout_error();
  test_lsp_manager_startup_timeout_and_validation();
  test_lsp_manager_containment_cleanup();
  test_lsp_manager_cancellation_precedence();
  test_lsp_manager_cancellation();
  test_lsp_manager_huge_response_caps();
  test_lsp_diagnostics_tool_and_dispatcher_json();
  test_lsp_file_uri_escapes_encoded_separators();
  test_lsp_dispatcher_redacts_server_error_context();
  test_lsp_dispatcher_preserves_safe_error_context_only();
  test_lsp_dispatcher_bounds_provider_json();
  test_lsp_configured_provider_loads_project_config_lazily();
  test_lsp_configured_provider_loads_global_config_from_safe_cwd();
  test_lsp_configured_provider_timeout_defaults();
  test_lsp_configured_provider_inspection_does_not_launch_servers();
  test_lsp_configured_provider_rejects_invalid_config();
}
