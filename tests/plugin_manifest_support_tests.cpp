#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "ava/plugin/manifest.h"
#include "ava/plugin/manifest_support.h"
#include "tests/support/test_harness.h"

namespace {

void write_text(std::filesystem::path const& path, std::string const& text)
{
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output << text;
}

std::string valid_manifest_json()
{
  return R"({
    "schema_version": 1,
    "id": "com.example.support",
    "name": "Support Plugin",
    "version": "0.1.0",
    "api_version": "ava.plugin.v1",
    "entrypoint": {"command": "node", "args": ["plugin.js", "--stdio"]},
    "capabilities": ["tools", "event_hooks"],
    "contributes": {
      "tools": [{"name": "todo_add", "description": "Add", "input_schema": {"type": "object"}}],
      "commands": [{"name": "todo.list", "description": "List"}],
      "prompts": [{"name": "review", "description": "Review", "path": "prompts/review.md"}],
      "skills": [{"name": "triage", "description": "Triage", "path": "skills/triage.md"}],
      "event_hooks": [{"event": "tool.result"}]
    }
  })";
}

void test_manifest_identifier_validators()
{
  expect(ava::plugin::detail::is_valid_plugin_id("com.example.support") &&
             ava::plugin::detail::is_valid_plugin_id("tool_1-alpha"),
         "plugin manifest support accepts stable lowercase plugin ids");
  expect(!ava::plugin::detail::is_valid_plugin_id("Com.example") &&
             !ava::plugin::detail::is_valid_plugin_id("bad..id") && !ava::plugin::detail::is_valid_plugin_id("bad-"),
         "plugin manifest support rejects unsafe plugin ids");

  expect(ava::plugin::detail::is_valid_contribution_name("tool.result_1") &&
             ava::plugin::detail::is_valid_contribution_name("Tool-Name"),
         "plugin manifest support accepts contribution names");
  expect(!ava::plugin::detail::is_valid_contribution_name("") &&
             !ava::plugin::detail::is_valid_contribution_name("bad/name"),
         "plugin manifest support rejects invalid contribution names");
}

void test_resource_path_validator()
{
  expect(ava::plugin::detail::is_valid_resource_path("prompts/review.md") &&
             ava::plugin::detail::is_valid_resource_path("skills/./triage.md"),
         "plugin manifest support accepts safe relative resource paths");
  expect(!ava::plugin::detail::is_valid_resource_path("/tmp/review.md") &&
             !ava::plugin::detail::is_valid_resource_path("../review.md") &&
             !ava::plugin::detail::is_valid_resource_path("prompts/\nreview.md"),
         "plugin manifest support rejects absolute, escaping, and control-byte paths");
}

void test_string_array_field()
{
  auto missing = ava::plugin::detail::string_array_field(R"({"entrypoint":{}})", "args");
  expect(missing && missing->empty(), "plugin manifest support treats missing string arrays as empty");

  auto parsed =
      ava::plugin::detail::string_array_field(R"({"args":["plugin.js","space value","quote\"value"]})", "args");
  expect(parsed && *parsed == std::vector<std::string>({"plugin.js", "space value", "quote\"value"}),
         parsed ? "plugin manifest support parses string-array fields"
                : "plugin manifest support parses string-array fields: " + parsed.error().format());

  auto non_string = ava::plugin::detail::string_array_field(R"({"args":["ok",7]})", "args");
  expect(!non_string && non_string.error().message().find("only strings") != std::string::npos,
         "plugin manifest support rejects non-string array values");

  auto bad_separator = ava::plugin::detail::string_array_field(R"({"args":["one" "two"]})", "args");
  expect(!bad_separator && bad_separator.error().message().find("invalid separator") != std::string::npos,
         "plugin manifest support rejects malformed string-array separators");
}

void test_entrypoint_and_contribution_parsing()
{
  auto entrypoint = ava::plugin::detail::parse_plugin_entrypoint(valid_manifest_json());
  expect(
      entrypoint && entrypoint->command == "node" && entrypoint->args.size() == 2 && entrypoint->args[1] == "--stdio",
      entrypoint ? "plugin manifest support parses entrypoints"
                 : "plugin manifest support parses entrypoints: " + entrypoint.error().format());

  auto missing_entrypoint = ava::plugin::detail::parse_plugin_entrypoint(R"({"schema_version":1})");
  expect(!missing_entrypoint && missing_entrypoint.error().message().find("entrypoint object") != std::string::npos,
         "plugin manifest support rejects missing entrypoint objects");

  auto contributions = ava::plugin::detail::parse_plugin_contributions(valid_manifest_json());
  expect(contributions && contributions->tools.size() == 1 && contributions->commands.size() == 1 &&
             contributions->prompts.size() == 1 && contributions->skills.size() == 1 &&
             contributions->event_hooks.size() == 1 &&
             contributions->tools[0].input_schema_json.find("\"type\"") != std::string::npos,
         contributions ? "plugin manifest support parses all contribution groups"
                       : "plugin manifest support parses all contribution groups: " + contributions.error().format());

  auto bad_resource = ava::plugin::detail::parse_plugin_contributions(
      R"({"contributes":{"prompts":[{"name":"review","path":"../escape.md"}]}})");
  expect(!bad_resource && bad_resource.error().message().find("safe relative path") != std::string::npos,
         "plugin manifest support rejects escaping resource contribution paths");

  auto bad_tool = ava::plugin::detail::parse_plugin_contributions(
      R"({"contributes":{"tools":[{"name":"bad/tool","input_schema":{"type":"object"}}]}})");
  expect(!bad_tool && bad_tool.error().message().find("valid name") != std::string::npos,
         "plugin manifest support rejects invalid tool contribution names");
}

void test_manifest_file_reads()
{
  auto const root = temp_root() / "plugin-manifest-support";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);

  auto const manifest_path = root / "plugin.json";
  write_text(manifest_path, valid_manifest_json());
  auto content = ava::plugin::detail::read_plugin_manifest_file(manifest_path);
  expect(content && content->find("\"schema_version\"") != std::string::npos,
         content ? "plugin manifest support reads regular manifest files"
                 : "plugin manifest support reads regular manifest files: " + content.error().format());

  auto const directory = root / "directory";
  std::filesystem::create_directories(directory);
  auto directory_read = ava::plugin::detail::read_plugin_manifest_file(directory);
  expect(!directory_read && directory_read.error().message().find("regular file") != std::string::npos,
         "plugin manifest support rejects directories as manifest files");

  auto const oversized = root / "oversized.json";
  write_text(oversized, std::string(ava::plugin::detail::kMaxPluginManifestBytes + 1, 'x'));
  auto oversized_read = ava::plugin::detail::read_plugin_manifest_file(oversized);
  expect(!oversized_read && oversized_read.error().message().find("maximum size") != std::string::npos,
         "plugin manifest support rejects oversized manifest files");
}

void test_public_manifest_parse_uses_support_helpers()
{
  auto parsed = ava::plugin::parse_plugin_manifest(valid_manifest_json(), "/tmp/plugin/plugin.json");
  expect(parsed && parsed->id == "com.example.support" && parsed->entrypoint.command == "node" &&
             parsed->contributes.prompts[0].path == "prompts/review.md" &&
             parsed->directory == std::filesystem::path("/tmp/plugin"),
         parsed ? "plugin manifest public parser still projects full manifests"
                : "plugin manifest public parser still projects full manifests: " + parsed.error().format());
}

}  // namespace

void run_plugin_manifest_support_tests()
{
  test_manifest_identifier_validators();
  test_resource_path_validator();
  test_string_array_field();
  test_entrypoint_and_contribution_parsing();
  test_manifest_file_reads();
  test_public_manifest_parse_uses_support_helpers();
}
