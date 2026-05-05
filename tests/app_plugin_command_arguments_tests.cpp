#include <string_view>

#include "ava/app/plugin_command_arguments.h"
#include "tests/support/test_harness.h"

namespace {

void test_plugin_command_arguments_parse_run()
{
  auto parsed = ava::app::detail::parse_plugin_run_arguments(
      R"( run com.example.plugin hello {"name":"ava","nested":{"ok":true}})");

  expect(parsed.has_value(), "plugin command arguments parse /plugin run arguments");
  if (parsed) {
    expect(parsed->plugin_id == "com.example.plugin" && parsed->command_name == "hello" &&
               parsed->arguments_json == R"({"name":"ava","nested":{"ok":true}})",
           "plugin command arguments preserve plugin id, command name, and JSON arguments");
  }

  auto default_args = ava::app::detail::parse_plugin_run_arguments("run com.example.plugin hello");
  expect(default_args && default_args->arguments_json == "{}",
         "plugin command arguments default missing command arguments to an empty JSON object");
}

void test_plugin_command_arguments_reject_invalid_run()
{
  auto invalid_subcommand = ava::app::detail::parse_plugin_run_arguments("start com.example.plugin hello {}");
  expect(!invalid_subcommand && invalid_subcommand.error().message().find("usage: /plugin run") != std::string::npos,
         "plugin command arguments require the run subcommand");

  auto invalid_json = ava::app::detail::parse_plugin_run_arguments("run com.example.plugin hello [bad]");
  expect(!invalid_json && invalid_json.error().message() == "plugin command arguments must be a JSON object",
         "plugin command arguments reject non-object JSON command arguments");

  auto missing = ava::app::detail::parse_plugin_run_arguments("run com.example.plugin");
  expect(!missing && missing.error().message().find("usage: /plugin run") != std::string::npos,
         "plugin command arguments report usage for incomplete run arguments");
}

void test_plugin_command_argument_tokens()
{
  expect(ava::app::detail::trim_ascii_whitespace(" \tvalue\r\n") == "value",
         "plugin command arguments trim ASCII whitespace");
  expect(ava::app::detail::plugin_validate_argument(" validate   ./plugin.json  ") == "./plugin.json",
         "plugin command arguments extract validate paths after the subcommand");

  std::string_view tokens = "  one two";
  auto first = ava::app::detail::consume_token(tokens);
  auto second = ava::app::detail::consume_token(tokens);
  auto third = ava::app::detail::consume_token(tokens);
  expect(first && *first == "one" && second && *second == "two" && !third,
         "plugin command arguments consume whitespace-delimited tokens");
}

}  // namespace

void run_app_plugin_command_arguments_tests()
{
  test_plugin_command_arguments_parse_run();
  test_plugin_command_arguments_reject_invalid_run();
  test_plugin_command_argument_tokens();
}
