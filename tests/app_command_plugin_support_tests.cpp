#include <string_view>

#include "ava/app/command_plugin_support.h"
#include "ava/plugin/diagnostics.h"
#include "ava/plugin/manifest.h"
#include "tests/support/test_harness.h"

namespace {

ava::plugin::PluginManifest manifest_with_command()
{
  ava::plugin::PluginManifest manifest;
  manifest.id = "com.example.plugin";
  manifest.name = "Example Plugin";
  manifest.version = "1.0.0";
  manifest.entrypoint.command = "plugin.sh";
  manifest.entrypoint.args = {"--safe"};
  manifest.capabilities = {"commands", std::string("line\nbreak")};
  manifest.contributes.commands.push_back(
      ava::plugin::PluginCommandContribution{.name = "hello", .description = "Say hello"});
  return manifest;
}

void test_plugin_command_support_parses_run_arguments()
{
  auto parsed = ava::app::detail::parse_plugin_run_arguments(
      R"( run com.example.plugin hello {"name":"ava","nested":{"ok":true}})");

  expect(parsed.has_value(), "plugin command support parses /plugin run arguments");
  if (parsed) {
    expect(parsed->plugin_id == "com.example.plugin" && parsed->command_name == "hello" &&
               parsed->arguments_json == R"({"name":"ava","nested":{"ok":true}})",
           "plugin command support preserves plugin id, command name, and JSON arguments");
  }

  auto default_args = ava::app::detail::parse_plugin_run_arguments("run com.example.plugin hello");
  expect(default_args && default_args->arguments_json == "{}",
         "plugin command support defaults missing command arguments to an empty JSON object");

  auto invalid_json = ava::app::detail::parse_plugin_run_arguments("run com.example.plugin hello [bad]");
  expect(!invalid_json && invalid_json.error().message() == "plugin command arguments must be a JSON object",
         "plugin command support rejects non-object JSON command arguments");

  auto missing = ava::app::detail::parse_plugin_run_arguments("run com.example.plugin");
  expect(!missing && missing.error().message().find("usage: /plugin run") != std::string::npos,
         "plugin command support reports usage for incomplete run arguments");
}

void test_plugin_command_support_token_helpers()
{
  expect(ava::app::detail::trim_ascii_whitespace(" \tvalue\r\n") == "value",
         "plugin command support trims ASCII whitespace");
  expect(ava::app::detail::plugin_validate_argument(" validate   ./plugin.json  ") == "./plugin.json",
         "plugin command support extracts validate paths after the subcommand");

  std::string_view tokens = "  one two";
  auto first = ava::app::detail::consume_token(tokens);
  auto second = ava::app::detail::consume_token(tokens);
  auto third = ava::app::detail::consume_token(tokens);
  expect(first && *first == "one" && second && *second == "two" && !third,
         "plugin command support consumes whitespace-delimited tokens");
}

void test_plugin_command_support_manifest_helpers()
{
  auto manifest = manifest_with_command();
  auto const* command = ava::app::detail::find_plugin_command(manifest, "hello");
  auto const* missing = ava::app::detail::find_plugin_command(manifest, "missing");
  expect(command && command->description == "Say hello" && missing == nullptr,
         "plugin command support finds contributed commands by name");
  expect(ava::app::detail::plugin_entrypoint_text(manifest.entrypoint) == "plugin.sh --safe",
         "plugin command support formats entrypoint text");
  expect(ava::app::detail::plugin_capabilities_text(manifest) == "commands, line?break",
         "plugin command support sanitizes capability text");
}

void test_plugin_command_support_not_found_text()
{
  ava::plugin::PluginDiagnostics diagnostics;
  diagnostics.failures.push_back(ava::plugin::PluginFailure{.scope = ava::plugin::PluginScope::Project,
                                                            .path = "plugin.json",
                                                            .message = "duplicate plugin id discovered",
                                                            .details = "plugin=com.example.plugin"});

  expect(ava::app::detail::has_duplicate_plugin_failure(diagnostics, "com.example.plugin"),
         "plugin command support detects duplicate plugin id failures");
  expect(ava::app::detail::plugin_not_found_text(diagnostics, "com.example.plugin").find("plugin id is ambiguous") !=
             std::string::npos,
         "plugin command support reports ambiguous plugin ids");
  expect(
      ava::app::detail::plugin_not_found_text(diagnostics, std::string("bad\nid")).find("bad?id") != std::string::npos,
      "plugin command support sanitizes plugin ids in not-found messages");
}

}  // namespace

void run_app_command_plugin_support_tests()
{
  test_plugin_command_support_parses_run_arguments();
  test_plugin_command_support_token_helpers();
  test_plugin_command_support_manifest_helpers();
  test_plugin_command_support_not_found_text();
}
