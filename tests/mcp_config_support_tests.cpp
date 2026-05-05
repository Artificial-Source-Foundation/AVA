#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "ava/mcp/config.h"
#include "ava/mcp/config_support.h"
#include "tests/support/test_harness.h"

namespace {

void write_text(std::filesystem::path const& path, std::string const& text)
{
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output << text;
}

bool has_context(ava::core::Error const& error, std::string const& key, std::string const& value)
{
  for (auto const& context : error.context()) {
    if (context.key == key && context.value == value) return true;
  }
  return false;
}

void test_forbidden_bytes_and_bool_fields()
{
  expect(!ava::mcp::detail::has_forbidden_byte("clean value"),
         "MCP config support accepts printable command text");
  expect(ava::mcp::detail::has_forbidden_byte("bad\nvalue") &&
             ava::mcp::detail::has_forbidden_byte(std::string("bad") + static_cast<char>(0x7F)),
         "MCP config support rejects control bytes");

  auto const enabled = ava::mcp::detail::bool_field("{\"enabled\": true, \"other\": false}", "enabled");
  auto const disabled = ava::mcp::detail::bool_field("{\"enabled\":false}", "enabled");
  auto const malformed = ava::mcp::detail::bool_field("{\"enabled\":trueish}", "enabled");
  expect(enabled && *enabled && disabled && !*disabled && !malformed,
         "MCP config support parses only strict JSON bool fields");
}

void test_array_and_string_array_fields()
{
  auto const array = ava::mcp::detail::array_field("{\"args\":[\"--stdio\", [\"nested\"], \"]\"]}", "args");
  expect(array && *array == "[\"--stdio\", [\"nested\"], \"]\"]", "MCP config support extracts complete arrays");
  expect(!ava::mcp::detail::array_field("{\"args\":\"--stdio\"}", "args"),
         "MCP config support rejects non-array values for array extraction");

  auto missing = ava::mcp::detail::string_array_field("{\"command\":\"demo\"}", "args");
  expect(missing && missing->empty(), "MCP config support treats missing string arrays as empty");

  auto parsed = ava::mcp::detail::string_array_field("{\"args\":[\"--stdio\",\"space value\",\"quote\\\"value\"]}",
                                                     "args");
  expect(parsed && *parsed == std::vector<std::string>({"--stdio", "space value", "quote\"value"}),
         parsed ? "MCP config support parses string-array values"
                : "MCP config support parses string-array values: " + parsed.error().format());

  auto non_string = ava::mcp::detail::string_array_field("{\"args\":[\"ok\", 7]}", "args");
  expect(!non_string && non_string.error().message().find("only strings") != std::string::npos,
         "MCP config support rejects non-string array members");

  auto bad_separator = ava::mcp::detail::string_array_field("{\"args\":[\"one\" \"two\"]}", "args");
  expect(!bad_separator && bad_separator.error().message().find("invalid separator") != std::string::npos,
         "MCP config support rejects malformed string-array separators");
}

void test_bounded_file_reads()
{
  auto const root = temp_root() / "mcp-config-support-files";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);

  auto const path = root / "mcp.json";
  write_text(path, "{\"servers\":[]}");
  auto content = ava::mcp::detail::read_mcp_config_file(path);
  expect(content && *content == "{\"servers\":[]}",
         content ? "MCP config support reads regular config files"
                 : "MCP config support reads regular config files: " + content.error().format());

  auto const directory = root / "directory";
  std::filesystem::create_directories(directory);
  auto directory_read = ava::mcp::detail::read_mcp_config_file(directory);
  expect(!directory_read && directory_read.error().message().find("regular file") != std::string::npos,
         "MCP config support rejects config directories");

  auto const oversized = root / "oversized.json";
  write_text(oversized, std::string(ava::mcp::detail::kMaxMcpConfigBytes + 1, 'x'));
  auto oversized_read = ava::mcp::detail::read_mcp_config_file(oversized);
  expect(!oversized_read && oversized_read.error().message().find("maximum size") != std::string::npos,
         "MCP config support rejects oversized config files");
}

void test_optional_config_loading_and_defaults()
{
  auto const root = temp_root() / "mcp-config-support-load";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);

  auto missing =
      ava::mcp::detail::load_optional_mcp_config(root / "missing.json", ava::mcp::McpServerScope::Global);
  expect(missing && missing->servers.empty() && missing->global_config_file == root / "missing.json",
         missing ? "MCP config support returns empty configs for missing optional files"
                 : "MCP config support returns empty configs for missing optional files: " + missing.error().format());

  auto const project_path = root / "workspace" / ".ava" / "mcp.json";
  write_text(project_path, "{\"servers\":[{\"id\":\"demo\",\"name\":\"Demo\",\"command\":\"demo\"}]}");
  auto project = ava::mcp::detail::load_optional_mcp_config(project_path, ava::mcp::McpServerScope::Project);
  expect(project && project->servers.size() == 1 && project->servers[0].id == "demo" &&
             !project->servers[0].enabled && project->project_config_file == project_path,
         project ? "MCP config support parses existing optional project configs"
                 : "MCP config support parses existing optional project configs: " + project.error().format());

  auto defaults = ava::mcp::default_mcp_config_options(root / "workspace");
  expect(defaults.workspace_dir == root / "workspace" &&
             defaults.project_config_file == root / "workspace" / ".ava" / "mcp.json" &&
             defaults.global_config_file.filename() == "mcp.json",
         "MCP config defaults include project and global config paths");
}

void test_config_append_duplicate_detection()
{
  ava::mcp::McpConfig target;
  target.global_config_file = "/tmp/global-mcp.json";
  target.servers.push_back(ava::mcp::McpServerConfig{.id = "global-demo",
                                                     .name = "Global Demo",
                                                     .command = "demo",
                                                     .args = {},
                                                     .enabled = true,
                                                     .scope = ava::mcp::McpServerScope::Global,
                                                     .source_path = "/tmp/global-mcp.json"});

  ava::mcp::McpConfig unique;
  unique.project_config_file = "/tmp/project-mcp.json";
  unique.servers.push_back(ava::mcp::McpServerConfig{.id = "project-demo",
                                                     .name = "Project Demo",
                                                     .command = "demo",
                                                     .args = {"--stdio"},
                                                     .enabled = false,
                                                     .scope = ava::mcp::McpServerScope::Project,
                                                     .source_path = "/tmp/project-mcp.json"});

  auto appended = ava::mcp::detail::append_mcp_config(target, std::move(unique));
  expect(appended && target.servers.size() == 2 && target.project_config_file == "/tmp/project-mcp.json",
         appended ? "MCP config support appends unique server configs"
                  : "MCP config support appends unique server configs: " + appended.error().format());

  ava::mcp::McpConfig duplicate;
  duplicate.project_config_file = "/tmp/project-duplicate-mcp.json";
  duplicate.servers.push_back(ava::mcp::McpServerConfig{.id = "global-demo",
                                                        .name = "Duplicate",
                                                        .command = "demo",
                                                        .args = {},
                                                        .enabled = false,
                                                        .scope = ava::mcp::McpServerScope::Project,
                                                        .source_path = "/tmp/project-duplicate-mcp.json"});

  auto duplicate_result = ava::mcp::detail::append_mcp_config(target, std::move(duplicate));
  expect(!duplicate_result && duplicate_result.error().message().find("duplicate") != std::string::npos &&
             has_context(duplicate_result.error(), "server", "global-demo"),
         "MCP config support reports duplicate server ids with semantic context");
}

}  // namespace

void run_mcp_config_support_tests()
{
  test_forbidden_bytes_and_bool_fields();
  test_array_and_string_array_fields();
  test_bounded_file_reads();
  test_optional_config_loading_and_defaults();
  test_config_append_duplicate_detection();
}
