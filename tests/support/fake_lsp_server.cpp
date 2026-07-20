#include "ava/core/json.h"
#include "ava/core/path.h"      // ava::core::logical_cwd

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <signal.h>
#include <unistd.h>

namespace {

enum class Mode
{
  Normal,
  EchoUriDiagnostics,
  SleepInitialize,
  SleepInitializeMarker,
  DelayedInitialize,
  MalformedDiagnostics,
  CrashDiagnostics,
  SleepDiagnostics,
  SleepDiagnosticsMarker,
  HugeContentLength,
  HugeHeader,
  MalformedSymbols,
  CwdMarker,
  TermIgnoringDescendantDiagnostics,
  LeaderExitsFirstDiagnostics,
  LeaderExitsAfterMarkerDiagnostics,
  SlowDidOpenDefinition,
};

struct Options
{
  Mode mode = Mode::Normal;
  std::string marker_path;
  std::string descendant_marker_path;
  std::string launch_marker_path;
  std::string environment_marker_path;
};

ssize_t read_retry(char* data, std::size_t size)
{
  while (true)
  {
    auto const bytes = read(STDIN_FILENO, data, size);
    if (bytes < 0 && errno == EINTR)
      continue;
    return bytes;
  }
}

std::optional<std::string> read_exact(std::size_t size, bool slow)
{
  std::string result(size, '\0');
  std::size_t offset = 0;
  while (offset < size)
  {
    auto const requested = slow ? std::min<std::size_t>(4096, size - offset) : size - offset;
    auto const bytes = read_retry(result.data() + offset, requested);
    if (bytes <= 0)
      return std::nullopt;
    offset += static_cast<std::size_t>(bytes);
    if (slow)
      usleep(1000);
  }
  return result;
}

std::optional<std::size_t> content_length(std::string_view header)
{
  constexpr std::string_view key = "Content-Length:";
  auto const position = header.find(key);
  if (position == std::string_view::npos)
    return std::nullopt;
  std::size_t index = position + key.size();
  while (index < header.size() && (header[index] == ' ' || header[index] == '\t')) ++index;
  std::size_t end = index;
  while (end < header.size() && std::isdigit(static_cast<unsigned char>(header[end])) != 0) ++end;
  if (end == index)
    return std::nullopt;
  return static_cast<std::size_t>(std::stoull(std::string(header.substr(index, end - index))));
}

std::optional<std::string> read_message(Options const& options)
{
  std::string header;
  char ch = '\0';
  while (header.find("\r\n\r\n") == std::string::npos)
  {
    auto const bytes = read_retry(&ch, 1);
    if (bytes <= 0)
      return std::nullopt;
    header.push_back(ch);
  }
  auto const length = content_length(header);
  if (!length)
    return std::nullopt;
  return read_exact(*length, options.mode == Mode::SlowDidOpenDefinition);
}

void write_message(std::string_view body)
{
  std::cout << "Content-Length: " << body.size() << "\r\n\r\n" << body;
  std::cout.flush();
}

Options parse_options(int argc, char** argv)
{
  Options options;
  for (int index = 1; index < argc; ++index)
  {
    if (std::strcmp(argv[index], "--malformed-diagnostics") == 0)
      options.mode = Mode::MalformedDiagnostics;
    if (std::strcmp(argv[index], "--crash-diagnostics") == 0)
      options.mode = Mode::CrashDiagnostics;
    if (std::strcmp(argv[index], "--echo-uri-diagnostics") == 0)
      options.mode = Mode::EchoUriDiagnostics;
    if (std::strcmp(argv[index], "--sleep-initialize") == 0)
      options.mode = Mode::SleepInitialize;
    if (std::strcmp(argv[index], "--sleep-initialize-marker") == 0 && index + 1 < argc)
    {
      options.mode = Mode::SleepInitializeMarker;
      options.marker_path = argv[++index];
    }
    if (std::strcmp(argv[index], "--delayed-initialize") == 0)
      options.mode = Mode::DelayedInitialize;
    if (std::strcmp(argv[index], "--launch-marker") == 0 && index + 1 < argc)
      options.launch_marker_path = argv[++index];
    if (std::strcmp(argv[index], "--sleep-diagnostics") == 0)
      options.mode = Mode::SleepDiagnostics;
    if (std::strcmp(argv[index], "--sleep-diagnostics-marker") == 0 && index + 1 < argc)
    {
      options.mode = Mode::SleepDiagnosticsMarker;
      options.marker_path = argv[++index];
    }
    if (std::strcmp(argv[index], "--huge-content-length") == 0)
      options.mode = Mode::HugeContentLength;
    if (std::strcmp(argv[index], "--huge-header") == 0)
      options.mode = Mode::HugeHeader;
    if (std::strcmp(argv[index], "--malformed-symbols") == 0)
      options.mode = Mode::MalformedSymbols;
    if (std::strcmp(argv[index], "--cwd-marker") == 0 && index + 1 < argc)
    {
      options.mode = Mode::CwdMarker;
      options.marker_path = argv[++index];
    }
    if (std::strcmp(argv[index], "--term-ignoring-descendant-diagnostics-markers") == 0 && index + 2 < argc)
    {
      options.mode = Mode::TermIgnoringDescendantDiagnostics;
      options.marker_path = argv[++index];
      options.descendant_marker_path = argv[++index];
    }
    if (std::strcmp(argv[index], "--leader-exits-first-diagnostics-markers") == 0 && index + 2 < argc)
    {
      options.mode = Mode::LeaderExitsFirstDiagnostics;
      options.marker_path = argv[++index];
      options.descendant_marker_path = argv[++index];
    }
    if (std::strcmp(argv[index], "--leader-exits-after-marker-diagnostics-markers") == 0 && index + 2 < argc)
    {
      options.mode = Mode::LeaderExitsAfterMarkerDiagnostics;
      options.marker_path = argv[++index];
      options.descendant_marker_path = argv[++index];
    }
    if (std::strcmp(argv[index], "--slow-did-open-definition") == 0)
      options.mode = Mode::SlowDidOpenDefinition;
    if (std::strcmp(argv[index], "--environment-marker") == 0 && index + 1 < argc)
      options.environment_marker_path = argv[++index];
  }
  return options;
}

void write_process_group_marker(std::string const& path)
{
  if (path.empty())
    return;
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file << static_cast<long long>(getpid()) << ' ' << static_cast<long long>(getpgrp()) << '\n';
}

void spawn_term_ignoring_descendant(Options const& options)
{
  write_process_group_marker(options.marker_path);
  pid_t const descendant = fork();
  if (descendant < 0)
    return;
  if (descendant == 0)
  {
    struct sigaction ignored{};
    ignored.sa_handler = SIG_IGN;
    sigemptyset(&ignored.sa_mask);
    sigaction(SIGTERM, &ignored, nullptr);
    write_process_group_marker(options.descendant_marker_path);
    for (int attempt = 0; attempt < 50; ++attempt) usleep(100000);
    _exit(0);
  }
}

void write_launch_marker(std::string const& path)
{
  if (path.empty())
    return;
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file << "started\n";
}

void write_environment_marker(std::string const& path)
{
  if (path.empty())
    return;
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  for (auto const* name : {"HOME", "LANG", "LC_ALL", "OPENAI_API_KEY", "AVA_LSP_TEST_SECRET", "AVA_UNRELATED"})
  {
    auto const* value = std::getenv(name);
    file << name << '=' << (value == nullptr ? "<unset>" : value) << '\n';
  }
}

void write_cwd_marker(std::string const& path)
{
  if (path.empty())
    return;
  // Prefer $PWD (logical path preserved by the shell) over getcwd() (physical path).
  auto cwd = ava::core::logical_cwd();
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file << cwd.string() << '\n';
}

void respond_initialize(long long id)
{
  std::string const body = "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
                           ",\"result\":{\"capabilities\":{\"diagnosticProvider\":{\"interFileDependencies\":false,"
                           "\"workspaceDiagnostics\":false},\"documentSymbolProvider\":true,"
                           "\"workspaceSymbolProvider\":true,\"definitionProvider\":true,\"referencesProvider\":true}}}";
  write_message(body);
}

std::string workspace_main_uri()
{
  auto cwd = ava::core::logical_cwd();
  return "file://" + cwd.string() + "/main.cpp";
}

std::string request_uri(std::string_view message)
{
  auto const params = ava::core::json::object_field(message, "params");
  auto const document = params ? ava::core::json::object_field(*params, "textDocument") : std::nullopt;
  auto const uri = document ? ava::core::json::string_field(*document, "uri") : std::nullopt;
  return uri.value_or(std::string{});
}

void respond_diagnostics(long long id, Options const& options, std::string_view uri, bool initialized)
{
  auto const mode = options.mode;
  if (mode == Mode::DelayedInitialize && !initialized)
  {
    write_message("{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) + ",\"error\":{\"code\":-32002,\"message\":\"initialized notification required\"}}");
    return;
  }
  if (mode == Mode::CrashDiagnostics)
    std::exit(23);
  if (mode == Mode::TermIgnoringDescendantDiagnostics)
  {
    spawn_term_ignoring_descendant(options);
    return;
  }
  if (mode == Mode::LeaderExitsFirstDiagnostics)
  {
    spawn_term_ignoring_descendant(options);
    _exit(23);
  }
  if (mode == Mode::LeaderExitsAfterMarkerDiagnostics)
  {
    spawn_term_ignoring_descendant(options);
    usleep(500000);
    _exit(23);
  }
  if (mode == Mode::SleepDiagnostics || mode == Mode::SleepDiagnosticsMarker)
  {
    write_process_group_marker(options.marker_path);
    usleep(1000000);
    return;
  }
  if (mode == Mode::MalformedDiagnostics)
  {
    write_message("not-json");
    return;
  }
  if (mode == Mode::HugeContentLength)
  {
    std::cout << "Content-Length: 4194305\r\n\r\n";
    std::cout.flush();
    return;
  }
  if (mode == Mode::HugeHeader)
  {
    std::cout << std::string(70 * 1024, 'X');
    std::cout.flush();
    return;
  }
  auto const message = mode == Mode::EchoUriDiagnostics ? std::string(uri) : std::string("fake diagnostic from LSP");
  std::string const body = "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
                           ",\"result\":{\"kind\":\"full\",\"items\":[{\"range\":{\"start\":{\"line\":2,"
                           "\"character\":4},\"end\":{\"line\":2,\"character\":9}},\"severity\":1,"
                           "\"code\":\"AVA_FAKE\",\"source\":\"ava_fake_lsp\","
                           "\"message\":\"" +
                           ava::core::json::escape(message) + "\"}]}}";
  write_message(body);
}

void respond_document_symbols(long long id, Options const& options)
{
  if (options.mode == Mode::MalformedSymbols)
  {
    std::string const body = "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) + ",\"result\":[{\"name\":\"broken\",\"kind\":12}]}";
    write_message(body);
    return;
  }
  std::string const body = "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
                           ",\"result\":[{\"name\":\"main\",\"kind\":12,\"range\":{\"start\":{\"line\":0,"
                           "\"character\":4},\"end\":{\"line\":0,\"character\":8}},\"selectionRange\":{\"start\":{"
                           "\"line\":0,\"character\":4},\"end\":{\"line\":0,\"character\":8}},\"children\":[{"
                           "\"name\":\"return statement\",\"kind\":12,\"range\":{\"start\":{\"line\":0,"
                           "\"character\":13},\"end\":{\"line\":0,\"character\":21}},\"selectionRange\":{\"start\":{"
                           "\"line\":0,\"character\":13},\"end\":{\"line\":0,\"character\":21}}}]}]}";
  write_message(body);
}

void respond_workspace_symbols(long long id)
{
  auto const uri = workspace_main_uri();
  std::string const body = "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
                           ",\"result\":[{\"name\":\"main\",\"kind\":12,\"containerName\":\"global\","
                           "\"location\":{\"uri\":\"" +
                           ava::core::json::escape(uri) +
                           "\",\"range\":{\"start\":{\"line\":0,\"character\":4},\"end\":{\"line\":0,"
                           "\"character\":8}}}}]}";
  write_message(body);
}

void respond_definition(long long id)
{
  auto const uri = workspace_main_uri();
  std::string const body = "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) + ",\"result\":{\"uri\":\"" + ava::core::json::escape(uri) +
                           "\",\"range\":{\"start\":{\"line\":0,\"character\":4},\"end\":{\"line\":0,"
                           "\"character\":8}}}}";
  write_message(body);
}

void respond_references(long long id, bool did_open)
{
  if (!did_open)
  {
    std::string const body = "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) + ",\"result\":[]}";
    write_message(body);
    return;
  }
  auto const uri = workspace_main_uri();
  std::string const body = "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) + ",\"result\":[{\"uri\":\"" + ava::core::json::escape(uri) +
                           "\",\"range\":{\"start\":{\"line\":0,\"character\":4},\"end\":{\"line\":0,"
                           "\"character\":8}}},{\"uri\":\"" +
                           ava::core::json::escape(uri) +
                           "\",\"range\":{\"start\":{\"line\":0,\"character\":13},\"end\":{\"line\":0,"
                           "\"character\":17}}}]}";
  write_message(body);
}

}  // namespace

int main(int argc, char** argv)
{
  auto const options = parse_options(argc, argv);
  write_launch_marker(options.launch_marker_path);
  write_environment_marker(options.environment_marker_path);
  bool did_open = false;
  bool initialized = false;
  while (auto message = read_message(options))
  {
    auto const method = ava::core::json::string_field(*message, "method");
    auto const id = ava::core::json::integer_field(*message, "id");
    if (!method)
      continue;
    if (*method == "initialize" && id)
    {
      if (options.mode == Mode::SleepInitialize || options.mode == Mode::SleepInitializeMarker)
      {
        write_process_group_marker(options.marker_path);
        usleep(1000000);
        continue;
      }
      if (options.mode == Mode::DelayedInitialize)
        usleep(500000);
      if (options.mode == Mode::CwdMarker)
        write_cwd_marker(options.marker_path);
      respond_initialize(*id);
    }
    else if (*method == "initialized")
    {
      initialized = true;
    }
    else if (*method == "textDocument/diagnostic" && id)
    {
      respond_diagnostics(*id, options, request_uri(*message), initialized);
    }
    else if (*method == "textDocument/documentSymbol" && id)
    {
      respond_document_symbols(*id, options);
    }
    else if (*method == "workspace/symbol" && id)
    {
      respond_workspace_symbols(*id);
    }
    else if (*method == "textDocument/definition" && id)
    {
      respond_definition(*id);
    }
    else if (*method == "textDocument/didOpen")
    {
      did_open = true;
      if (options.mode == Mode::SlowDidOpenDefinition)
        usleep(250000);
    }
    else if (*method == "textDocument/references" && id)
    {
      respond_references(*id, did_open);
    }
    else if (*method == "exit")
    {
      return 0;
    }
  }
  return 0;
}
