#include "sys.h"
#include "ava/lsp/builtin_recipes.h"
#include "ava/lsp/lsp_client.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

int main()
{
  std::string fixture_template = (std::filesystem::temp_directory_path() / "ava-clangd-smoke-XXXXXX").string();
  auto* created = ::mkdtemp(fixture_template.data());
  if (created == nullptr)
  {
    std::cerr << "failed to create private clangd smoke fixture\n";
    return 1;
  }
  auto const fixture = std::filesystem::path(created);
  static_cast<void>(::chmod(fixture.c_str(), S_IRWXU));
  [[maybe_unused]] auto cleanup = std::shared_ptr<void>(nullptr, [fixture](void*) {
    std::error_code ignored;
    std::filesystem::remove_all(fixture, ignored);
  });

  auto const source_path = fixture / "main.cpp";
  {
    std::ofstream source(source_path, std::ios::binary | std::ios::trunc);
    source << "int helper() { return 1; }\nint main() {\n  return helper();\n}\n";
  }
  auto inspections = ava::lsp::inspect_builtin_servers({"clangd"}, fixture);
  auto const clangd = std::ranges::find_if(inspections, [](auto const& item) { return item.id == "clangd"; });
  if (clangd == inspections.end() || clangd->status != ava::lsp::BuiltinServerStatus::Available || !clangd->executable)
    return 77;

  auto client = ava::lsp::SubprocessLspClient::start({
      .argv = {clangd->executable->canonical_path.string(), "--background-index"},
      .workspace_root = fixture,
      .server_root = fixture,
      .process_cwd = fixture,
      .startup_timeout = std::chrono::milliseconds(5000),
      .request_timeout = std::chrono::milliseconds(5000),
      .language_id = "cpp",
      .executable_identity = clangd->executable,
  });
  if (!client)
  {
    std::cerr << "clangd initialize failed\n";
    return 1;
  }
  auto definitions = (*client)->definitions(source_path, 2, 9);
  if (!definitions || definitions->empty() || definitions->front().path != source_path)
  {
    std::cerr << "clangd definition operation failed\n";
    return 1;
  }
  return 0;
}
