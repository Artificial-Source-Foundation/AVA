#include "ava/core/json.h"

#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

#include <unistd.h>

namespace {

enum class Mode
{
  Normal,
  EchoUriDiagnostics,
  SleepInitialize,
  SleepInitializeMarker,
  MalformedDiagnostics,
  CrashDiagnostics,
  SleepDiagnostics,
  SleepDiagnosticsMarker,
  HugeContentLength,
  HugeHeader,
  MalformedSymbols,
  CwdMarker,
};

struct Options
{
  Mode mode = Mode::Normal;
  std::string marker_path;
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

std::optional<std::string> read_exact(std::size_t size)
{
  std::string result(size, '\0');
  std::size_t offset = 0;
  while (offset < size)
  {
    auto const bytes = read_retry(result.data() + offset, size - offset);
    if (bytes <= 0)
      return std::nullopt;
    offset += static_cast<std::size_t>(bytes);
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

std::optional<std::string> read_message()
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
  return read_exact(*length);
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
  }
  return options;
}

void write_process_group_marker(std::string const& path)
{
  if (path.empty())
    return;
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file << static_cast<long long>(getpgrp()) << '\n';
}

void write_cwd_marker(std::string const& path)
{
  if (path.empty())
    return;
  char buffer[4096]{};
  if (getcwd(buffer, sizeof(buffer)) == nullptr)
    return;
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file << buffer << '\n';
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
  char buffer[4096]{};
  if (getcwd(buffer, sizeof(buffer)) == nullptr)
    return "file:///workspace/main.cpp";
  return "file://" + std::string(buffer) + "/main.cpp";
}

std::string request_uri(std::string_view message)
{
  auto const params = ava::core::json::object_field(message, "params");
  auto const document = params ? ava::core::json::object_field(*params, "textDocument") : std::nullopt;
  auto const uri = document ? ava::core::json::string_field(*document, "uri") : std::nullopt;
  return uri.value_or(std::string{});
}

void respond_diagnostics(long long id, Options const& options, std::string_view uri)
{
  auto const mode = options.mode;
  if (mode == Mode::CrashDiagnostics)
    std::exit(23);
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
    std::string const body = "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
                             ",\"result\":[{\"name\":\"broken\",\"kind\":12}]}";
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
                           "\"location\":{\"uri\":\"" + ava::core::json::escape(uri) +
                           "\",\"range\":{\"start\":{\"line\":0,\"character\":4},\"end\":{\"line\":0,"
                           "\"character\":8}}}}]}";
  write_message(body);
}

void respond_definition(long long id)
{
  auto const uri = workspace_main_uri();
  std::string const body = "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
                           ",\"result\":{\"uri\":\"" + ava::core::json::escape(uri) +
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
  std::string const body = "{\"jsonrpc\":\"2.0\",\"id\":" + std::to_string(id) +
                           ",\"result\":[{\"uri\":\"" + ava::core::json::escape(uri) +
                           "\",\"range\":{\"start\":{\"line\":0,\"character\":4},\"end\":{\"line\":0,"
                           "\"character\":8}}},{\"uri\":\"" + ava::core::json::escape(uri) +
                           "\",\"range\":{\"start\":{\"line\":0,\"character\":13},\"end\":{\"line\":0,"
                           "\"character\":17}}}]}";
  write_message(body);
}

}  // namespace

int main(int argc, char** argv)
{
  auto const options = parse_options(argc, argv);
  bool did_open = false;
  while (auto message = read_message())
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
      if (options.mode == Mode::CwdMarker)
        write_cwd_marker(options.marker_path);
      respond_initialize(*id);
    }
    else if (*method == "textDocument/diagnostic" && id)
    {
      respond_diagnostics(*id, options, request_uri(*message));
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
