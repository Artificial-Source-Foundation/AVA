#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "ava/agent/tool_dispatcher.h"
#include "ava/app/plugin_event_hooks.h"
#include "ava/core/json.h"
#include "ava/permissions/permission.h"
#include "ava/plugin/discovery.h"
#include "ava/plugin/enablement.h"
#include "ava/plugin/manifest.h"
#include "ava/plugin/protocol.h"
#include "ava/plugin/runner.h"
#include "ava/plugin/stream_buffers.h"
#include "ava/plugin/tool_broker.h"
#include "ava/tools/file_tools.h"
#include "tests/support/test_harness.h"

namespace {

std::string valid_manifest_json(std::string id = "com.example.todo")
{
  return std::string("{\n") +
         "  \"schema_version\": 1,\n"
         "  \"id\": \"" +
         ava::core::json::escape(id) +
         "\",\n"
         "  \"name\": \"Todo Tools\",\n"
         "  \"version\": \"0.1.0\",\n"
         "  \"api_version\": \"ava.plugin.v1\",\n"
         "  \"description\": \"test plugin\",\n"
         "  \"entrypoint\": {\"command\": \"node\", \"args\": [\"plugin.js\", \"--safe\"]},\n"
         "  \"capabilities\": [\"tools\", \"commands\"],\n"
         "  \"contributes\": {\n"
         "    \"tools\": [{\"name\": \"todo_add\", \"description\": \"Add todo\", \"input_schema\": {\"type\": "
         "\"object\", \"additionalProperties\": false}}],\n"
         "    \"commands\": [{\"name\": \"todo\", \"description\": \"Show todos\"}],\n"
         "    \"prompts\": [{\"name\": \"review\", \"description\": \"Review prompt\", \"path\": "
         "\"prompts/review.md\"}],\n"
         "    \"skills\": [{\"name\": \"triage\", \"description\": \"Triage skill\", \"path\": "
         "\"skills/triage.md\"}],\n"
         "    \"event_hooks\": [{\"event\": \"tool.result\"}]\n"
         "  },\n"
         "  \"unknown_future_field\": true\n"
         "}";
}

void write_text(std::filesystem::path const& path, std::string const& text)
{
  std::filesystem::create_directories(path.parent_path());
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file << text;
}

std::string read_text(std::filesystem::path const& path)
{
  std::ifstream file(path, std::ios::binary);
  std::ostringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

std::string runner_manifest_json(std::string id, std::string script_name)
{
  return std::string("{\n") +
         "  \"schema_version\": 1,\n"
         "  \"id\": \"" +
         ava::core::json::escape(id) +
         "\",\n"
         "  \"name\": \"Runner Plugin\",\n"
         "  \"version\": \"0.1.0\",\n"
         "  \"api_version\": \"ava.plugin.v1\",\n"
         "  \"entrypoint\": {\"command\": \"/bin/sh\", \"args\": [\"" +
         ava::core::json::escape(script_name) +
         "\"]},\n"
         "  \"capabilities\": [\"tools\"],\n"
         "  \"contributes\": {\"tools\": [], \"commands\": []}\n"
         "}";
}

std::string tool_manifest_json(std::string id, std::string script_name, std::string tool_name = "todo_add")
{
  return std::string("{\n") +
         "  \"schema_version\": 1,\n"
         "  \"id\": \"" +
         ava::core::json::escape(id) +
         "\",\n"
         "  \"name\": \"Tool Plugin\",\n"
         "  \"version\": \"0.1.0\",\n"
         "  \"api_version\": \"ava.plugin.v1\",\n"
         "  \"entrypoint\": {\"command\": \"/bin/sh\", \"args\": [\"" +
         ava::core::json::escape(script_name) +
         "\"]},\n"
         "  \"capabilities\": [\"tools\"],\n"
         "  \"contributes\": {\"tools\": [{\"name\": \"" +
         ava::core::json::escape(tool_name) +
         "\", \"description\": \"Add todo\", \"input_schema\": {\"type\": \"object\", \"properties\": "
         "{\"text\": {\"type\": \"string\"}}, \"required\": [\"text\"], \"additionalProperties\": false}}], "
         "\"commands\": []}\n"
         "}";
}

std::string event_hook_manifest_json(std::string id, std::string script_name, std::string event_name = "tool.result")
{
  return std::string("{\n") +
         "  \"schema_version\": 1,\n"
         "  \"id\": \"" +
         ava::core::json::escape(id) +
         "\",\n"
         "  \"name\": \"Event Plugin\",\n"
         "  \"version\": \"0.1.0\",\n"
         "  \"api_version\": \"ava.plugin.v1\",\n"
         "  \"entrypoint\": {\"command\": \"/bin/sh\", \"args\": [\"" +
         ava::core::json::escape(script_name) +
         "\"]},\n"
         "  \"capabilities\": [\"event_hooks\"],\n"
         "  \"contributes\": {\"event_hooks\": [{\"event\": \"" +
         ava::core::json::escape(event_name) +
         "\"}]}\n"
         "}";
}

ava::plugin::PluginManifest runner_manifest(std::filesystem::path const& plugin_dir, std::string id,
                                            std::string script_name)
{
  auto parsed = ava::plugin::parse_plugin_manifest(runner_manifest_json(std::move(id), std::move(script_name)),
                                                   plugin_dir / "plugin.json");
  expect(parsed.has_value(),
         parsed ? "runner plugin manifest parses" : "runner plugin manifest parses: " + parsed.error().format());
  return parsed.value_or(ava::plugin::PluginManifest{});
}

ava::plugin::PluginRunnerOptions runner_options(std::filesystem::path const& workspace,
                                                std::chrono::milliseconds startup_timeout)
{
  ava::plugin::PluginRunnerOptions options;
  options.workspace_dir = workspace;
  options.startup_timeout = startup_timeout;
  options.max_record_bytes = 64 * 1024;
  options.max_stderr_bytes = 64 * 1024;
  return options;
}

void test_plugin_manifest_parsing()
{
  auto parsed = ava::plugin::parse_plugin_manifest(valid_manifest_json(), "/tmp/plugin/plugin.json");
  expect(parsed.has_value(), parsed ? "plugin manifest parses valid JSON"
                                    : "plugin manifest parses valid JSON: " + parsed.error().format());
  if (parsed) {
    expect(parsed->id == "com.example.todo", "plugin manifest id parsed");
    expect(parsed->entrypoint.command == "node", "plugin manifest entrypoint command parsed");
    expect(parsed->entrypoint.args.size() == 2 && parsed->entrypoint.args[1] == "--safe",
           "plugin manifest entrypoint args parsed");
    expect(parsed->capabilities.size() == 2, "plugin manifest capabilities parsed");
    expect(parsed->contributes.tools.size() == 1, "plugin manifest tool contribution parsed");
    expect(parsed->contributes.tools[0].input_schema_json.find("additionalProperties") != std::string::npos,
           "plugin manifest preserves tool input schema JSON");
    expect(parsed->contributes.commands.size() == 1 && parsed->contributes.commands[0].name == "todo",
           "plugin manifest command contribution parsed");
    expect(parsed->contributes.prompts.size() == 1 && parsed->contributes.prompts[0].path == "prompts/review.md",
           "plugin manifest prompt contribution parsed");
    expect(parsed->contributes.skills.size() == 1 && parsed->contributes.skills[0].name == "triage",
           "plugin manifest skill contribution parsed");
    expect(parsed->contributes.event_hooks.size() == 1 && parsed->contributes.event_hooks[0].event == "tool.result",
           "plugin manifest event hook contribution parsed");
    expect(parsed->directory == parsed->path.parent_path(), "plugin manifest records directory");
  }

  auto bad_api = ava::plugin::parse_plugin_manifest(
      "{\"schema_version\":1,\"id\":\"com.example.bad\",\"name\":\"Bad\",\"version\":\"0.1\",\"api_version\":\"wrong\","
      "\"entrypoint\":{\"command\":\"node\"}}",
      {});
  expect(!bad_api && bad_api.error().message().find("api_version") != std::string::npos,
         "plugin manifest rejects unsupported API versions");

  auto bad_id = ava::plugin::parse_plugin_manifest(valid_manifest_json("Bad.Id"), {});
  expect(!bad_id && bad_id.error().message().find("id") != std::string::npos,
         "plugin manifest rejects non-lowercase ids");

  auto bad_schema = ava::plugin::parse_plugin_manifest(
      "{\"schema_version\":1,\"id\":\"com.example.bad\",\"name\":\"Bad\",\"version\":\"0.1\",\"api_version\":\"ava."
      "plugin.v1\",\"entrypoint\":{\"command\":\"node\"},\"contributes\":{\"tools\":[{\"name\":\"bad\",\"input_"
      "schema\":{not-json}}]}}",
      {});
  expect(!bad_schema && bad_schema.error().message().find("JSON object") != std::string::npos,
         "plugin manifest rejects malformed tool schemas");

  auto bad_args = ava::plugin::parse_plugin_manifest(
      "{\"schema_version\":1,\"id\":\"com.example.bad\",\"name\":\"Bad\",\"version\":\"0.1\",\"api_"
      "version\":\"ava.plugin.v1\",\"entrypoint\":{\"command\":\"node\",\"args\":[{\"bad\":\"arg\"}]}}",
      {});
  expect(!bad_args && bad_args.error().message().find("only strings") != std::string::npos,
         "plugin manifest rejects non-string entrypoint args");

  auto bad_resource_path = ava::plugin::parse_plugin_manifest(
      "{\"schema_version\":1,\"id\":\"com.example.bad\",\"name\":\"Bad\",\"version\":\"0.1\",\"api_"
      "version\":\"ava.plugin.v1\",\"entrypoint\":{\"command\":\"node\"},\"contributes\":{\"prompts\":[{\"name\":"
      "\"bad\","
      "\"path\":\"../escape.md\"}]}}",
      {});
  expect(!bad_resource_path && bad_resource_path.error().message().find("safe relative path") != std::string::npos,
         "plugin manifest rejects prompt and skill paths that escape the plugin directory");
}

void test_plugin_discovery()
{
  auto const root = temp_root() / "plugins";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const global_manifest = root / "config" / "ava" / "plugins" / "com.example.global" / "plugin.json";
  auto const project_manifest = root / "workspace" / ".ava" / "plugins" / "com.example.project" / "plugin.json";
  write_text(global_manifest, valid_manifest_json("com.example.global"));
  write_text(project_manifest, valid_manifest_json("com.example.project"));
  write_text(root / "workspace" / ".ava" / "plugins" / "com.example.project" / "should_not_run", "not executable");

  auto discovered = ava::plugin::discover_plugins(ava::plugin::PluginDiscoveryOptions{
      .global_plugins_dir = root / "config" / "ava" / "plugins",
      .project_plugins_dir = root / "workspace" / ".ava" / "plugins",
  });
  expect(discovered.has_value(),
         discovered ? "plugin discovery loads global and project manifests"
                    : "plugin discovery loads global and project manifests: " + discovered.error().format());
  if (discovered) {
    expect(discovered->size() == 2, "plugin discovery finds two manifests");
    bool const has_global =
        std::any_of(discovered->begin(), discovered->end(), [](ava::plugin::DiscoveredPlugin const& plugin) {
          return plugin.manifest.id == "com.example.global" && plugin.scope == ava::plugin::PluginScope::Global;
        });
    bool const has_project =
        std::any_of(discovered->begin(), discovered->end(), [](ava::plugin::DiscoveredPlugin const& plugin) {
          return plugin.manifest.id == "com.example.project" && plugin.scope == ava::plugin::PluginScope::Project;
        });
    expect(has_global && has_project, "plugin discovery preserves plugin scope");
  }

  auto const duplicate_root = root / "duplicates";
  write_text(duplicate_root / "global" / "com.example.same" / "plugin.json", valid_manifest_json("com.example.same"));
  write_text(duplicate_root / "project" / "com.example.same" / "plugin.json", valid_manifest_json("com.example.same"));
  auto duplicate = ava::plugin::discover_plugins(ava::plugin::PluginDiscoveryOptions{
      .global_plugins_dir = duplicate_root / "global",
      .project_plugins_dir = duplicate_root / "project",
  });
  expect(!duplicate && duplicate.error().message().find("duplicate") != std::string::npos,
         "plugin discovery rejects duplicate plugin ids across scopes");

  auto const symlink_root = root / "symlink";
  auto const outside = symlink_root / "outside";
  write_text(outside / "plugin.json", valid_manifest_json("com.example.symlinked"));
  std::filesystem::create_directories(symlink_root / "global");
  std::error_code symlink_error;
  std::filesystem::create_directory_symlink(outside, symlink_root / "global" / "com.example.symlinked", symlink_error);
  if (!symlink_error) {
    std::error_code broken_symlink_error;
    std::filesystem::create_directory_symlink(symlink_root / "missing", symlink_root / "global" / "com.example.broken",
                                              broken_symlink_error);
    auto skipped = ava::plugin::discover_plugins(ava::plugin::PluginDiscoveryOptions{
        .global_plugins_dir = symlink_root / "global",
        .project_plugins_dir = {},
    });
    expect(skipped && skipped->empty(), "plugin discovery skips symlinked plugin directories");
  }

  auto const oversized = root / "oversized" / "plugin.json";
  write_text(oversized, std::string(300 * 1024, '{'));
  auto oversized_manifest = ava::plugin::load_plugin_manifest(oversized);
  expect(!oversized_manifest && oversized_manifest.error().message().find("maximum size") != std::string::npos,
         "plugin manifest load rejects oversized files before parsing");

  auto const directory_manifest = root / "directory-manifest" / "plugin.json";
  std::filesystem::create_directories(directory_manifest);
  auto non_regular_manifest = ava::plugin::load_plugin_manifest(directory_manifest);
  expect(!non_regular_manifest && non_regular_manifest.error().message().find("regular file") != std::string::npos,
         "plugin manifest load rejects non-regular files");
}

void test_plugin_enablement()
{
  auto const root = temp_root() / "plugin-enable";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const state_file = root / "state" / "ava" / "plugin-enablement.json";
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);

  auto initially_enabled = ava::plugin::plugin_enabled(state_file, workspace, "com.example.todo");
  expect(initially_enabled && !*initially_enabled, "plugins are disabled by default");

  auto enabled = ava::plugin::set_plugin_enabled(state_file, workspace, "com.example.todo", true);
  expect(enabled.has_value(), "plugin enablement state can be written");
  auto after_enable = ava::plugin::plugin_enabled(state_file, workspace, "com.example.todo");
  expect(after_enable && *after_enable, "plugin enablement state can be read");

  auto disabled = ava::plugin::set_plugin_enabled(state_file, workspace, "com.example.todo", false);
  expect(disabled.has_value(), "plugin disablement state can be written");
  auto after_disable = ava::plugin::plugin_enabled(state_file, workspace, "com.example.todo");
  expect(after_disable && !*after_disable, "plugin disablement state can be read");

  auto records = ava::plugin::load_plugin_enablement(state_file);
  expect(records && records->size() == 1, "plugin enablement file keeps one workspace/plugin record");
  if (records && !records->empty()) {
    expect(records->front().workspace == ava::plugin::canonical_workspace_key(workspace),
           "plugin enablement stores canonical workspace key");
    expect(records->front().scope == ava::plugin::PluginScope::Project, "plugin enablement stores plugin scope");
  }

  auto global_enable = ava::plugin::set_plugin_enabled(state_file, workspace, "com.example.todo", true,
                                                       ava::plugin::PluginScope::Global);
  expect(global_enable.has_value(), "global plugin enablement can be stored separately");
  auto project_state =
      ava::plugin::plugin_enabled(state_file, workspace, "com.example.todo", ava::plugin::PluginScope::Project);
  auto global_state =
      ava::plugin::plugin_enabled(state_file, workspace, "com.example.todo", ava::plugin::PluginScope::Global);
  expect(project_state && !*project_state && global_state && *global_state,
         "global and project plugin enablement are isolated");

  auto invalid_id = ava::plugin::set_plugin_enabled(state_file, workspace, "Bad.Id", true);
  expect(!invalid_id && invalid_id.error().message().find("plugin id") != std::string::npos,
         "plugin enablement rejects invalid plugin ids");

  auto const malformed_state = root / "state" / "ava" / "malformed-plugin-enablement.json";
  write_text(malformed_state,
             "{\"workspaces\":{\"/tmp/work\":{\"project\":{\"com.example.todo\":{\"enabled\":truefalse}}}}}");
  auto malformed = ava::plugin::load_plugin_enablement(malformed_state);
  expect(!malformed && malformed.error().message().find("valid JSON") != std::string::npos,
         "plugin enablement rejects malformed JSON instead of partial parsing");

  auto const escaped_state = root / "state" / "ava" / "escaped-plugin-enablement.json";
  write_text(escaped_state,
             "{\"workspaces\":{\"/tmp/work{\\\"q\\\"}\":{\"project\":{\"com.example.todo\":{\"enabled\":true}}}}}");
  auto escaped = ava::plugin::load_plugin_enablement(escaped_state);
  expect(escaped && escaped->size() == 1 && escaped->front().workspace == std::filesystem::path("/tmp/work{\"q\"}") &&
             escaped->front().enabled,
         "plugin enablement parses escaped keys and braces inside strings");
}

void test_plugin_protocol_helpers()
{
  auto const init_request =
      ava::plugin::plugin_initialize_request_json(ava::plugin::kPluginApiVersion, "com.example.todo", "/tmp/work");
  expect(init_request.find("\"type\":\"initialize\"") != std::string::npos &&
             init_request.find("\"plugin_id\":\"com.example.todo\"") != std::string::npos &&
             init_request.find("\"workspace\":\"/tmp/work\"") != std::string::npos,
         "plugin protocol formats initialize requests");

  auto const tool_request = ava::plugin::plugin_tool_call_request_json("ava_tool_call_1", "todo_add",
                                                                       "{\"text\":\"hi\"}", "call_1", "/tmp/work");
  expect(tool_request.find("\"type\":\"tool.call\"") != std::string::npos &&
             tool_request.find("\"tool\":\"todo_add\"") != std::string::npos &&
             tool_request.find("\"call_id\":\"call_1\"") != std::string::npos,
         "plugin protocol formats tool call requests");

  auto const command_request = ava::plugin::plugin_command_call_request_json(
      "ava_command_cmd_1", "todo", "{\"filter\":\"open\"}", "cmd_1", "/tmp/work");
  expect(command_request.find("\"type\":\"command.call\"") != std::string::npos &&
             command_request.find("\"command\":\"todo\"") != std::string::npos,
         "plugin protocol formats command call requests");

  auto const event_request = ava::plugin::plugin_event_observe_request_json(
      "ava_event_event_1", "tool_result", "{\"tool\":\"demo\"}", "event_1", "/tmp/work");
  expect(event_request.find("\"type\":\"event.observe\"") != std::string::npos &&
             event_request.find("\"event\":\"tool_result\"") != std::string::npos,
         "plugin protocol formats event observe requests");

  auto const initialized = ava::plugin::parse_plugin_initialized_response(
      "{\"id\":\"ava_1\",\"type\":\"initialized\",\"api_version\":\"ava.plugin.v1\",\"plugin_version\":\"0.1.0\","
      "\"contributions\":{\"tools\":[]}}");
  expect(initialized && initialized->plugin_version == "0.1.0" &&
             initialized->contributions_json.find("\"tools\":[]") != std::string::npos,
         initialized ? "plugin protocol parses initialized responses" : "plugin protocol parses initialized responses");

  auto const unsupported = ava::plugin::parse_plugin_initialized_response(
      "{\"id\":\"ava_1\",\"type\":\"initialized\",\"api_version\":\"wrong\",\"plugin_version\":\"0.1.0\","
      "\"contributions\":{}}");
  expect(!unsupported, "plugin protocol rejects unsupported initialized responses");

  expect(ava::plugin::plugin_json_depth_within_limit("{\"a\":[1]}", 2) &&
             !ava::plugin::plugin_json_depth_within_limit("{\"a\":[{\"b\":[]}]}", 2),
         "plugin protocol enforces JSON depth limits");

  auto const tool_result = ava::plugin::parse_plugin_tool_result_response(
      "{\"id\":\"ava_tool_call_1\",\"type\":\"tool.result\",\"ok\":true,\"content\":\"Added\","
      "\"metadata\":{\"count\":1}}",
      "ava_tool_call_1");
  expect(tool_result && tool_result->ok && tool_result->content == "Added" &&
             tool_result->metadata_json.find("\"count\":1") != std::string::npos,
         "plugin protocol parses tool results");

  auto const command_result = ava::plugin::parse_plugin_command_result_response(
      "{\"id\":\"ava_command_cmd_1\",\"type\":\"command.result\",\"ok\":true,\"content\":\"Done\"}",
      "ava_command_cmd_1");
  expect(command_result && command_result->ok && command_result->content == "Done",
         "plugin protocol parses command results");

  auto const event_result = ava::plugin::parse_plugin_event_observed_response(
      "{\"id\":\"ava_event_event_1\",\"type\":\"event.observed\",\"ok\":true}", "ava_event_event_1");
  expect(event_result && event_result->ok && event_result->content.empty(),
         "plugin protocol parses event observed responses with optional content");

  auto const wrong_id = ava::plugin::parse_plugin_tool_result_response(
      "{\"id\":\"wrong\",\"type\":\"tool.result\",\"ok\":true,\"content\":\"Added\"}", "ava_tool_call_1");
  expect(!wrong_id, "plugin protocol rejects response id mismatches");
}

void test_plugin_stream_buffers()
{
  ava::plugin::PluginRecordBuffer records(8);
  records.append("one\r\n");
  auto first = records.take_record();
  expect(first && *first == "one" && records.empty(), "plugin record buffer extracts CRLF-delimited records");

  records.append("two\nthree");
  auto second = records.take_record();
  expect(second && *second == "two" && records.size() == 5 && !records.exceeds_limit(),
         "plugin record buffer preserves buffered partial records after a newline");

  records.append("xxxx");
  expect(records.exceeds_limit(), "plugin record buffer reports newline-free records over the byte cap");
  records.trim_front_to_limit();
  expect(records.size() == 8, "plugin record buffer trims retained stdout to the configured cap");

  ava::plugin::PluginRecordBuffer oversized_line(4);
  oversized_line.append("12345\n");
  expect(oversized_line.exceeds_limit(), "plugin record buffer reports oversized delimited records");

  ava::plugin::PluginStderrTail tail(6);
  tail.append("abc");
  tail.append("defg");
  expect(tail.text() == "bcdefg" && tail.truncated(), "plugin stderr tail keeps only the newest bytes");
  tail.append("123456789");
  expect(tail.text() == "456789" && tail.truncated(), "plugin stderr tail bounds chunks larger than the cap");
}

void test_plugin_runner_initializes_and_shuts_down()
{
  auto const root = temp_root() / "plugin-runner";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const plugin_dir = root / "plugins" / "com.example.runner";
  std::filesystem::create_directories(workspace);

  write_text(plugin_dir / "plugin.sh",
             "read line\n"
             "printf '%s\\n' \"$line\" > init.txt\n"
             "printf '%s\\n' '0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ' >&2\n"
             "printf '%s\\n' '{\"id\":\"ava_1\",\"type\":\"initialized\",\"api_version\":\"ava."
             "plugin.v1\",\"plugin_version\":\"0.1.0\",\"contributions\":{\"tools\":[],\"commands\":[],"
             "\"prompts\":[],\"event_hooks\":[]}}'\n"
             "while read line; do :; done\n"
             "printf '%s\\n' stopped > stopped.txt\n");

  auto manifest = runner_manifest(plugin_dir, "com.example.runner", "plugin.sh");
  auto options = runner_options(workspace, std::chrono::milliseconds(500));
  options.max_stderr_bytes = 16;
  auto process = ava::plugin::PluginProcess::start(std::move(manifest), options);
  expect(process.has_value(), process ? "plugin runner initializes fake plugin"
                                      : "plugin runner initializes fake plugin: " + process.error().format());
  if (process) {
    expect((*process)->initialization().plugin_version == "0.1.0", "plugin runner records handshake version");
    expect((*process)->initialization().contributions_json.find("\"tools\":[]") != std::string::npos,
           "plugin runner records handshake contributions");
    expect((*process)->stderr_truncated(), "plugin runner bounds stderr diagnostics");
    expect((*process)->stderr_tail().size() == 16, "plugin runner keeps stderr tail");
    auto const init = read_text(plugin_dir / "init.txt");
    expect(init.find("\"type\":\"initialize\"") != std::string::npos &&
               init.find("\"plugin_id\":\"com.example.runner\"") != std::string::npos &&
               init.find(workspace.string()) != std::string::npos,
           "plugin runner sends initialize record with plugin and workspace");
    auto shutdown = (*process)->shutdown(std::chrono::milliseconds(500));
    expect(shutdown.has_value(), shutdown ? "plugin runner shuts down cleanly"
                                          : "plugin runner shuts down cleanly: " + shutdown.error().format());
    expect(read_text(plugin_dir / "stopped.txt").find("stopped") != std::string::npos,
           "plugin runner closes stdin so fake plugin can exit cleanly");
  }
}

void test_plugin_runner_accepts_buffered_extra_records()
{
  auto const root = temp_root() / "plugin-runner-buffered";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const plugin_dir = root / "plugins" / "com.example.buffered";
  std::filesystem::create_directories(workspace);

  write_text(plugin_dir / "plugin.sh",
             "read line\n"
             "printf '%s\\n' '{\"id\":\"ava_1\",\"type\":\"initialized\",\"api_version\":\"ava."
             "plugin.v1\",\"plugin_version\":\"0.1.0\",\"contributions\":{}}'\n"
             "printf '%s\\n' '" +
                 std::string(300, 'x') +
                 "'\n"
                 "while read line; do :; done\n");

  auto options = runner_options(workspace, std::chrono::milliseconds(500));
  options.max_record_bytes = 256;
  auto process =
      ava::plugin::PluginProcess::start(runner_manifest(plugin_dir, "com.example.buffered", "plugin.sh"), options);
  expect(process.has_value(),
         process ? "plugin runner accepts buffered records after initialize"
                 : "plugin runner accepts buffered records after initialize: " + process.error().format());
  if (process) {
    auto shutdown = (*process)->shutdown(std::chrono::milliseconds(500));
    expect(shutdown.has_value(), "plugin runner shuts down after buffered records");
  }
}

void test_plugin_runner_contained_failures()
{
  auto const root = temp_root() / "plugin-runner-failures";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);

  auto const malformed_dir = root / "plugins" / "com.example.malformed";
  write_text(malformed_dir / "plugin.sh",
             "read line\n"
             "printf '%s\\n' 'not-json'\n");
  auto malformed_options = runner_options(workspace, std::chrono::milliseconds(500));
  auto malformed = ava::plugin::PluginProcess::start(
      runner_manifest(malformed_dir, "com.example.malformed", "plugin.sh"), malformed_options);
  expect(!malformed && malformed.error().message().find("malformed") != std::string::npos,
         "plugin runner rejects malformed initialize records");

  auto const unsupported_dir = root / "plugins" / "com.example.unsupported";
  write_text(unsupported_dir / "plugin.sh",
             "read line\n"
             "printf '%s\\n' '{\"id\":\"ava_1\",\"type\":\"initialized\",\"api_version\":\"wrong\","
             "\"plugin_version\":\"0.1.0\",\"contributions\":{}}'\n");
  auto unsupported_options = runner_options(workspace, std::chrono::milliseconds(500));
  auto unsupported = ava::plugin::PluginProcess::start(
      runner_manifest(unsupported_dir, "com.example.unsupported", "plugin.sh"), unsupported_options);
  expect(!unsupported && unsupported.error().message().find("unsupported") != std::string::npos,
         "plugin runner rejects unsupported handshake API versions");

  std::string deep_contributions = "{}";
  for (int i = 0; i < 160; ++i) deep_contributions = "{\"x\":" + deep_contributions + "}";
  auto const deep_dir = root / "plugins" / "com.example.deep";
  write_text(deep_dir / "plugin.sh",
             "read line\n"
             "printf '%s\\n' '{\"id\":\"ava_1\",\"type\":\"initialized\",\"api_version\":\"ava."
             "plugin.v1\",\"plugin_version\":\"0.1.0\",\"contributions\":" +
                 deep_contributions + "}'\n");
  auto deep_options = runner_options(workspace, std::chrono::milliseconds(500));
  auto deep =
      ava::plugin::PluginProcess::start(runner_manifest(deep_dir, "com.example.deep", "plugin.sh"), deep_options);
  expect(!deep && deep.error().message().find("malformed") != std::string::npos,
         "plugin runner rejects deeply nested initialize JSON");

  auto const timeout_dir = root / "plugins" / "com.example.timeout";
  write_text(timeout_dir / "plugin.sh", "sleep 2\n");
  auto timeout_options = runner_options(workspace, std::chrono::milliseconds(100));
  auto timed_out = ava::plugin::PluginProcess::start(runner_manifest(timeout_dir, "com.example.timeout", "plugin.sh"),
                                                     timeout_options);
  expect(!timed_out && timed_out.error().message().find("timed out") != std::string::npos,
         "plugin runner times out hung startup");

  auto const startup_cancel_dir = root / "plugins" / "com.example.startupcancel";
  write_text(startup_cancel_dir / "plugin.sh", "sleep 2\n");
  auto startup_cancel_options = runner_options(workspace, std::chrono::milliseconds(1000));
  int startup_cancel_checks = 0;
  auto startup_canceled =
      ava::plugin::PluginProcess::start(runner_manifest(startup_cancel_dir, "com.example.startupcancel", "plugin.sh"),
                                        startup_cancel_options, [&] { return ++startup_cancel_checks > 2; });
  expect(!startup_canceled && startup_canceled.error().message().find("canceled") != std::string::npos,
         "plugin runner cancels hung startup before timeout");

  auto const exited_dir = root / "plugins" / "com.example.exited";
  write_text(exited_dir / "plugin.sh",
             "read line\n"
             "printf '%s\\n' 'plugin exited during initialize' >&2\n"
             "exit 7\n");
  auto exited_options = runner_options(workspace, std::chrono::milliseconds(500));
  auto exited =
      ava::plugin::PluginProcess::start(runner_manifest(exited_dir, "com.example.exited", "plugin.sh"), exited_options);
  auto const exited_format = exited ? std::string{} : exited.error().format();
  expect(!exited && exited_format.find("exit 7") != std::string::npos &&
             exited_format.find("plugin exited during initialize") != std::string::npos,
         "plugin runner reports early process exit status and stderr diagnostics: " + exited_format);

  auto const oversized_dir = root / "plugins" / "com.example.oversized";
  write_text(oversized_dir / "plugin.sh", "read line\nprintf '%s\\n' '" + std::string(200, 'x') + "'\n");
  auto oversized_options = runner_options(workspace, std::chrono::milliseconds(500));
  oversized_options.max_record_bytes = 64;
  auto oversized = ava::plugin::PluginProcess::start(
      runner_manifest(oversized_dir, "com.example.oversized", "plugin.sh"), oversized_options);
  expect(!oversized && oversized.error().message().find("size cap") != std::string::npos,
         "plugin runner rejects oversized protocol records");

  auto const flood_dir = root / "plugins" / "com.example.flood";
  write_text(flood_dir / "plugin.sh",
             "read line\n"
             "while :; do printf x; done\n");
  auto flood_options = runner_options(workspace, std::chrono::milliseconds(500));
  flood_options.max_record_bytes = 64;
  auto flood =
      ava::plugin::PluginProcess::start(runner_manifest(flood_dir, "com.example.flood", "plugin.sh"), flood_options);
  expect(!flood && flood.error().message().find("size cap") != std::string::npos,
         "plugin runner bounds newline-free stdout floods");
}

void test_plugin_runner_tool_calls()
{
  auto const root = temp_root() / "plugin-runner-tools";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const plugin_dir = root / "plugins" / "com.example.toolrunner";
  std::filesystem::create_directories(workspace);

  write_text(plugin_dir / "plugin.sh",
             "read line\n"
             "printf '%s\\n' '{\"id\":\"ava_1\",\"type\":\"initialized\",\"api_version\":\"ava."
             "plugin.v1\",\"plugin_version\":\"0.1.0\",\"contributions\":{}}'\n"
             "read line\n"
             "printf '%s\\n' \"$line\" > tool-request.txt\n"
             "printf '%s\\n' '{\"id\":\"ava_tool_call_1\",\"type\":\"tool.result\",\"ok\":true,"
             "\"content\":\"Added todo\",\"metadata\":{\"count\":1}}'\n"
             "while read line; do :; done\n");

  auto process = ava::plugin::PluginProcess::start(runner_manifest(plugin_dir, "com.example.toolrunner", "plugin.sh"),
                                                   runner_options(workspace, std::chrono::milliseconds(500)));
  expect(process.has_value(), process ? "plugin runner starts before tool call"
                                      : "plugin runner starts before tool call: " + process.error().format());
  if (process) {
    auto result = (*process)->call_tool("todo_add", "{\"text\":\"write tests\"}", "call_1");
    expect(result && result->ok && result->content == "Added todo" &&
               result->metadata_json.find("\"count\":1") != std::string::npos,
           result ? "plugin runner exchanges tool.call/tool.result records"
                  : "plugin runner exchanges tool.call/tool.result records: " + result.error().format());
    auto const request = read_text(plugin_dir / "tool-request.txt");
    expect(request.find("\"type\":\"tool.call\"") != std::string::npos &&
               request.find("\"tool\":\"todo_add\"") != std::string::npos &&
               request.find("\"text\":\"write tests\"") != std::string::npos &&
               request.find(workspace.string()) != std::string::npos,
           "plugin runner sends bounded tool call request context");
    auto shutdown = (*process)->shutdown(std::chrono::milliseconds(500));
    expect(shutdown.has_value(), "plugin runner shuts down after tool call");
  }

  auto const malformed_dir = root / "plugins" / "com.example.toolmalformed";
  write_text(malformed_dir / "plugin.sh",
             "read line\n"
             "printf '%s\\n' '{\"id\":\"ava_1\",\"type\":\"initialized\",\"api_version\":\"ava."
             "plugin.v1\",\"plugin_version\":\"0.1.0\",\"contributions\":{}}'\n"
             "read line\n"
             "printf '%s\\n' '{\"id\":\"ava_tool_bad\",\"type\":\"tool.result\",\"ok\":true}'\n");
  auto malformed =
      ava::plugin::PluginProcess::start(runner_manifest(malformed_dir, "com.example.toolmalformed", "plugin.sh"),
                                        runner_options(workspace, std::chrono::milliseconds(500)));
  expect(malformed.has_value(), "plugin runner starts malformed-result fake plugin");
  if (malformed) {
    auto result = (*malformed)->call_tool("todo_add", "{}", "bad");
    expect(!result && result.error().message().find("malformed") != std::string::npos,
           "plugin runner rejects malformed tool results");
  }

  auto const timeout_dir = root / "plugins" / "com.example.tooltimeout";
  write_text(timeout_dir / "plugin.sh",
             "read line\n"
             "printf '%s\\n' '{\"id\":\"ava_1\",\"type\":\"initialized\",\"api_version\":\"ava."
             "plugin.v1\",\"plugin_version\":\"0.1.0\",\"contributions\":{}}'\n"
             "read line\n"
             "sleep 2\n");
  auto timeout_options = runner_options(workspace, std::chrono::milliseconds(500));
  timeout_options.request_timeout = std::chrono::milliseconds(100);
  auto timeout = ava::plugin::PluginProcess::start(runner_manifest(timeout_dir, "com.example.tooltimeout", "plugin.sh"),
                                                   timeout_options);
  expect(timeout.has_value(), "plugin runner starts timeout fake plugin");
  if (timeout) {
    auto result = (*timeout)->call_tool("todo_add", "{}", "slow");
    expect(!result && result.error().message().find("timed out") != std::string::npos,
           "plugin runner times out hung tool calls independently from startup");
  }

  auto const cancel_dir = root / "plugins" / "com.example.toolcancel";
  write_text(cancel_dir / "plugin.sh",
             "read line\n"
             "printf '%s\\n' '{\"id\":\"ava_1\",\"type\":\"initialized\",\"api_version\":\"ava."
             "plugin.v1\",\"plugin_version\":\"0.1.0\",\"contributions\":{}}'\n"
             "read line\n"
             "sleep 2\n");
  auto cancel_options = runner_options(workspace, std::chrono::milliseconds(500));
  cancel_options.request_timeout = std::chrono::milliseconds(1000);
  auto cancel = ava::plugin::PluginProcess::start(runner_manifest(cancel_dir, "com.example.toolcancel", "plugin.sh"),
                                                  cancel_options);
  expect(cancel.has_value(), "plugin runner starts cancellation fake plugin");
  if (cancel) {
    int cancel_checks = 0;
    auto result = (*cancel)->call_tool("todo_add", "{}", "cancel", [&] { return ++cancel_checks > 2; });
    expect(!result && result.error().message().find("canceled") != std::string::npos,
           "plugin runner cancels hung tool calls before timeout");
  }

  auto const crashed_dir = root / "plugins" / "com.example.toolcrash";
  write_text(crashed_dir / "plugin.sh",
             "read line\n"
             "printf '%s\\n' '{\"id\":\"ava_1\",\"type\":\"initialized\",\"api_version\":\"ava."
             "plugin.v1\",\"plugin_version\":\"0.1.0\",\"contributions\":{}}'\n"
             "read line\n"
             "printf '%s\\n' 'plugin exited during tool call' >&2\n"
             "exit 9\n");
  auto crashed = ava::plugin::PluginProcess::start(runner_manifest(crashed_dir, "com.example.toolcrash", "plugin.sh"),
                                                   runner_options(workspace, std::chrono::milliseconds(500)));
  expect(crashed.has_value(), "plugin runner starts crash fake plugin");
  if (crashed) {
    auto result = (*crashed)->call_tool("todo_add", "{}", "crash");
    auto const result_format = result ? std::string{} : result.error().format();
    expect(!result && result_format.find("exit 9") != std::string::npos &&
               result_format.find("plugin exited during tool call") != std::string::npos,
           "plugin runner reports tool-time process exits with stderr diagnostics: " + result_format);
  }
}

void test_plugin_runner_command_calls()
{
  auto const root = temp_root() / "plugin-runner-commands";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const plugin_dir = root / "plugins" / "com.example.commandrunner";
  std::filesystem::create_directories(workspace);

  write_text(plugin_dir / "plugin.sh",
             "read line\n"
             "printf '%s\\n' '{\"id\":\"ava_1\",\"type\":\"initialized\",\"api_version\":\"ava."
             "plugin.v1\",\"plugin_version\":\"0.1.0\",\"contributions\":{}}'\n"
             "read line\n"
             "printf '%s\\n' \"$line\" > command-request.txt\n"
             "printf '%s\\n' '{\"id\":\"ava_command_cmd_1\",\"type\":\"command.result\",\"ok\":true,"
             "\"content\":\"Command complete\",\"metadata\":{\"count\":2}}'\n"
             "while read line; do :; done\n");

  auto process =
      ava::plugin::PluginProcess::start(runner_manifest(plugin_dir, "com.example.commandrunner", "plugin.sh"),
                                        runner_options(workspace, std::chrono::milliseconds(500)));
  expect(process.has_value(), process ? "plugin runner starts before command call"
                                      : "plugin runner starts before command call: " + process.error().format());
  if (process) {
    auto result = (*process)->call_command("todo", "{\"filter\":\"open\"}", "cmd_1");
    expect(result && result->ok && result->content == "Command complete" &&
               result->metadata_json.find("\"count\":2") != std::string::npos,
           result ? "plugin runner exchanges command.call/command.result records"
                  : "plugin runner exchanges command.call/command.result records: " + result.error().format());
    auto const request = read_text(plugin_dir / "command-request.txt");
    expect(request.find("\"type\":\"command.call\"") != std::string::npos &&
               request.find("\"command\":\"todo\"") != std::string::npos &&
               request.find("\"filter\":\"open\"") != std::string::npos &&
               request.find(workspace.string()) != std::string::npos,
           "plugin runner sends bounded command call request context");
    auto shutdown = (*process)->shutdown(std::chrono::milliseconds(500));
    expect(shutdown.has_value(), "plugin runner shuts down after command call");
  }
}

void test_plugin_runner_event_observation()
{
  auto const root = temp_root() / "plugin-runner-events";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const plugin_dir = root / "plugins" / "com.example.eventrunner";
  std::filesystem::create_directories(workspace);

  write_text(plugin_dir / "plugin.sh",
             "read line\n"
             "printf '%s\n' '{\"id\":\"ava_1\",\"type\":\"initialized\",\"api_version\":\"ava."
             "plugin.v1\",\"plugin_version\":\"0.1.0\",\"contributions\":{}}'\n"
             "read line\n"
             "printf '%s\n' \"$line\" > event-request.txt\n"
             "printf '%s\n' '{\"id\":\"ava_event_event_1\",\"type\":\"event.observed\",\"ok\":true,"
             "\"content\":\"observed\",\"metadata\":{\"count\":1}}'\n"
             "while read line; do :; done\n");

  auto process = ava::plugin::PluginProcess::start(runner_manifest(plugin_dir, "com.example.eventrunner", "plugin.sh"),
                                                   runner_options(workspace, std::chrono::milliseconds(500)));
  expect(process.has_value(), process ? "plugin runner starts before event observation"
                                      : "plugin runner starts before event observation: " + process.error().format());
  if (process) {
    auto result = (*process)->observe_event("tool_result", "{\"tool\":\"demo\"}", "event_1");
    expect(result && result->ok && result->content == "observed" &&
               result->metadata_json.find("\"count\":1") != std::string::npos,
           result ? "plugin runner exchanges event.observe/event.observed records"
                  : "plugin runner exchanges event.observe/event.observed records: " + result.error().format());
    auto const request = read_text(plugin_dir / "event-request.txt");
    expect(request.find("\"type\":\"event.observe\"") != std::string::npos &&
               request.find("\"event\":\"tool_result\"") != std::string::npos &&
               request.find("\"tool\":\"demo\"") != std::string::npos &&
               request.find(workspace.string()) != std::string::npos,
           "plugin runner sends bounded event observation request context");
    auto shutdown = (*process)->shutdown(std::chrono::milliseconds(500));
    expect(shutdown.has_value(), "plugin runner shuts down after event observation");
  }
}

void test_enabled_plugin_event_hooks_observe_runtime_events()
{
  auto const root = temp_root() / "plugin-event-hooks";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const project_plugins = workspace / ".ava" / "plugins";
  auto const plugin_dir = project_plugins / "com.example.events";
  auto const state_file = root / "state" / "plugin-enablement.json";
  std::filesystem::create_directories(workspace);

  write_text(plugin_dir / "plugin.json", event_hook_manifest_json("com.example.events", "plugin.sh"));
  write_text(plugin_dir / "plugin.sh",
             "read line\n"
             "printf '%s\n' '{\"id\":\"ava_1\",\"type\":\"initialized\",\"api_version\":\"ava."
             "plugin.v1\",\"plugin_version\":\"0.1.0\",\"contributions\":{}}'\n"
             "read line\n"
             "printf '%s\n' \"$line\" > event-request.txt\n"
             "printf '%s\n' '{\"id\":\"ava_event_call_hook\",\"type\":\"event.observed\",\"ok\":true}'\n"
             "while read line; do :; done\n");
  auto enabled = ava::plugin::set_plugin_enabled(state_file, workspace, "com.example.events", true,
                                                 ava::plugin::PluginScope::Project);
  expect(enabled.has_value(), "event hook plugin test enables project plugin");

  std::vector<ava::permissions::PermissionPrompt> prompts;
  bool forwarded = false;
  auto sink = ava::app::make_plugin_event_observer_sink(
      ava::app::PluginEventObserverOptions{
          .workspace_dir = workspace,
          .plugin_global_plugins_dir = root / "global-plugins",
          .plugin_project_plugins_dir = project_plugins,
          .plugin_enablement_file = state_file,
          .mode = ava::agent::Mode::Build,
          .permission_resolver = [&prompts](ava::permissions::PermissionPrompt const& prompt)
              -> ava::core::Result<ava::permissions::PermissionResolution> {
            prompts.push_back(prompt);
            return ava::permissions::PermissionResolution::Allow;
          }},
      [&forwarded](ava::app::RuntimeEvent const&) -> ava::core::VoidResult {
        forwarded = true;
        return {};
      });

  ava::app::RuntimeEvent event;
  event.type = ava::app::RuntimeEventType::ToolResult;
  event.call_id = "call_hook";
  event.tool_name = "demo";
  event.status = "success";
  event.text = "ok";
  auto observed = sink(event);
  expect(observed.has_value(), "plugin event observer sink forwards runtime event successfully");
  expect(forwarded, "plugin event observer forwards to the next runtime event sink");
  expect(prompts.size() == 2 && prompts[0].operation == ava::permissions::Operation::PluginExecute &&
             prompts[1].operation == ava::permissions::Operation::PluginEventObserve &&
             prompts[1].command == "com.example.events:tool.result",
         "plugin event hooks require plugin.execute and plugin.event.observe permission approval");
  auto const request = read_text(plugin_dir / "event-request.txt");
  expect(request.find("\"type\":\"event.observe\"") != std::string::npos &&
             request.find("\"event\":\"tool_result\"") != std::string::npos &&
             request.find("\"tool\":\"demo\"") != std::string::npos,
         "enabled plugin event hook observes matching runtime event aliases");
}

void test_plugin_event_hook_failures_report_to_opt_in_sink()
{
  struct CapturedFailure {
    std::string plugin_id;
    std::string event_name;
    ava::core::ErrorCategory category = ava::core::ErrorCategory::Unknown;
    std::string message;
    std::string details;
  };

  auto const root = temp_root() / "plugin-event-hook-failures";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const project_plugins = workspace / ".ava" / "plugins";
  auto const plugin_dir = project_plugins / "com.example.eventdiag";
  auto const state_file = root / "state" / "plugin-enablement.json";
  std::filesystem::create_directories(workspace);

  write_text(plugin_dir / "plugin.json", event_hook_manifest_json("com.example.eventdiag", "plugin.sh"));
  write_text(plugin_dir / "plugin.sh",
             "read line\n"
             "printf '%s\n' '{\"id\":\"ava_1\",\"type\":\"initialized\",\"api_version\":\"ava."
             "plugin.v1\",\"plugin_version\":\"0.1.0\",\"contributions\":{}}'\n"
             "read line\n"
             "printf '%s\n' \"$line\" > event-request.txt\n"
             "printf '%s\n' '{\"id\":\"ava_event_call_diag\",\"type\":\"tool.result\",\"ok\":true,"
             "\"content\":\"wrong response type\"}'\n"
             "while read line; do :; done\n");
  auto enabled = ava::plugin::set_plugin_enabled(state_file, workspace, "com.example.eventdiag", true,
                                                 ava::plugin::PluginScope::Project);
  expect(enabled.has_value(), "event hook diagnostic test enables project plugin");

  std::vector<CapturedFailure> failures;
  bool forwarded = false;
  auto sink = ava::app::make_plugin_event_observer_sink(
      ava::app::PluginEventObserverOptions{
          .workspace_dir = workspace,
          .plugin_global_plugins_dir = root / "global-plugins",
          .plugin_project_plugins_dir = project_plugins,
          .plugin_enablement_file = state_file,
          .mode = ava::agent::Mode::Build,
          .permission_resolver = [](ava::permissions::PermissionPrompt const&)
              -> ava::core::Result<ava::permissions::PermissionResolution> {
            return ava::permissions::PermissionResolution::Allow;
          },
          .hook_failure_sink =
              [&failures](std::string_view plugin_id, std::string_view event_name, ava::core::Error const& error) {
                failures.push_back(CapturedFailure{.plugin_id = std::string(plugin_id),
                                                   .event_name = std::string(event_name),
                                                   .category = error.category(),
                                                   .message = error.message(),
                                                   .details = error.format()});
              }},
      [&forwarded](ava::app::RuntimeEvent const&) -> ava::core::VoidResult {
        forwarded = true;
        return {};
      });

  ava::app::RuntimeEvent event;
  event.type = ava::app::RuntimeEventType::ToolResult;
  event.call_id = "call_diag";
  event.tool_name = "demo";
  event.status = "success";
  event.text = "ok";
  auto observed = sink(event);
  expect(observed.has_value(), "plugin event observer keeps hook failures best-effort");
  expect(forwarded, "plugin event observer forwards runtime events after hook failures");
  expect(failures.size() == 1, "plugin event observer reports hook protocol failures to opt-in sink");
  if (!failures.empty()) {
    expect(failures[0].plugin_id == "com.example.eventdiag", "hook failure sink receives plugin id");
    expect(failures[0].event_name == "tool.result", "hook failure sink receives contributed hook event name");
    expect(failures[0].category == ava::core::ErrorCategory::Tool, "hook failure sink receives error category");
    expect(failures[0].message.find("plugin event response is malformed") != std::string::npos,
           "hook failure sink receives error message");
    expect(failures[0].details.find("tool.result") != std::string::npos,
           "hook failure sink receives formatted error details");
  }
}

void test_plugin_tool_dispatcher()
{
  auto const root = temp_root() / "plugin-tool-dispatcher";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const project_plugins = workspace / ".ava" / "plugins";
  auto const plugin_dir = project_plugins / "com.example.todo";
  auto const state_file = root / "state" / "plugin-enablement.json";
  std::filesystem::create_directories(workspace);

  write_text(plugin_dir / "plugin.json", tool_manifest_json("com.example.todo", "plugin.sh"));
  write_text(plugin_dir / "plugin.sh",
             "read line\n"
             "printf '%s\\n' '{\"id\":\"ava_1\",\"type\":\"initialized\",\"api_version\":\"ava."
             "plugin.v1\",\"plugin_version\":\"0.1.0\",\"contributions\":{}}'\n"
             "read line\n"
             "printf '%s\\n' \"$line\" > tool-request.txt\n"
             "printf '%s\\n' '{\"id\":\"ava_tool_call_plugin\",\"type\":\"tool.result\",\"ok\":true,"
             "\"content\":\"Added via dispatcher\",\"metadata\":{\"count\":1}}'\n"
             "while read line; do :; done\n");
  auto enabled = ava::plugin::set_plugin_enabled(state_file, workspace, "com.example.todo", true,
                                                 ava::plugin::PluginScope::Project);
  expect(enabled.has_value(), "dispatcher plugin test enables project plugin");

  auto const model_tool_name = ava::plugin::plugin_model_tool_name("com.example.todo", "todo_add");
  std::vector<ava::permissions::PermissionPrompt> prompts;
  std::vector<ava::tools::PermissionAuditEvent> audits;
  bool cancel_requested = false;
  ava::tools::ToolContext context;
  context.workspace_dir = workspace;
  context.plugin_global_plugins_dir = root / "global-plugins";
  context.plugin_project_plugins_dir = project_plugins;
  context.plugin_enablement_file = state_file;
  context.permission_resolver = [&prompts](ava::permissions::PermissionPrompt const& prompt)
      -> ava::core::Result<ava::permissions::PermissionResolution> {
    prompts.push_back(prompt);
    return ava::permissions::PermissionResolution::Allow;
  };
  context.permission_audit_sink = [&audits](ava::tools::PermissionAuditEvent const& event) -> ava::core::VoidResult {
    audits.push_back(event);
    return {};
  };
  context.cancel_requested = [&] { return cancel_requested; };

  auto const schemas = ava::agent::ToolDispatcher::tool_schemas_json(context);
  bool const has_plugin_schema = std::any_of(schemas.begin(), schemas.end(), [&](std::string const& schema) {
    return schema.find("\"name\":\"" + model_tool_name + "\"") != std::string::npos &&
           schema.find("\"parameters\"") != std::string::npos;
  });
  expect(has_plugin_schema, "enabled plugin tool contribution is exported as a provider schema");

  ava::agent::ToolDispatcher dispatcher(context);
  auto dispatched = dispatcher.dispatch(ava::agent::ProviderToolCall{
      .id = "call_plugin", .name = model_tool_name, .arguments_json = "{\"text\":\"write tests\"}"});
  expect(dispatched && dispatched->success && dispatched->result_text.find("Added via dispatcher") != std::string::npos,
         dispatched ? "dispatcher calls enabled plugin tool through broker"
                    : "dispatcher calls enabled plugin tool through broker: " + dispatched.error().format());
  expect(dispatched && dispatched->payload.status == ava::agent::ToolResultStatus::Success &&
             dispatched->payload.content_type == "application/json" &&
             dispatched->payload.content.find("com.example.todo") != std::string::npos &&
             dispatched->payload.content.find("Added via dispatcher") != std::string::npos,
         "plugin tool dispatcher attaches structured semantic result payloads");
  expect(prompts.size() == 2 && prompts[0].operation == ava::permissions::Operation::PluginExecute &&
             prompts[1].operation == ava::permissions::Operation::PluginToolCall &&
             prompts[1].tool_name == model_tool_name && prompts[1].command == "com.example.todo:todo_add",
         "plugin tool calls require plugin.execute and plugin.tool.call permission approval");
  expect(!audits.empty() && audits.back().operation == ava::permissions::Operation::PluginToolCall &&
             audits.back().resolution == "allow",
         "plugin tool permission decisions are audited");
  auto const request = read_text(plugin_dir / "tool-request.txt");
  expect(request.find("\"type\":\"tool.call\"") != std::string::npos &&
             request.find("\"tool\":\"todo_add\"") != std::string::npos,
         "dispatcher broker sends plugin protocol tool call");

  auto invalid_args = dispatcher.dispatch(
      ava::agent::ProviderToolCall{.id = "call_invalid", .name = model_tool_name, .arguments_json = "[]"});
  expect(invalid_args && !invalid_args->success && invalid_args->result_text.find("JSON object") != std::string::npos,
         "plugin tool dispatcher rejects non-object arguments before execution");
  expect(invalid_args && invalid_args->payload.status == ava::agent::ToolResultStatus::Error &&
             invalid_args->payload.error_category == "invalid_argument",
         "plugin tool dispatcher attaches structured semantic error payloads");

  std::vector<ava::permissions::PermissionPrompt> denied_prompts;
  auto denied_context = context;
  denied_context.permission_resolver = [&denied_prompts](ava::permissions::PermissionPrompt const& prompt)
      -> ava::core::Result<ava::permissions::PermissionResolution> {
    denied_prompts.push_back(prompt);
    return ava::permissions::PermissionResolution::Deny;
  };
  ava::agent::ToolDispatcher denied_dispatcher(denied_context);
  auto denied = denied_dispatcher.dispatch(
      ava::agent::ProviderToolCall{.id = "call_denied", .name = model_tool_name, .arguments_json = "{}"});
  expect(denied && !denied->success &&
             denied->result_text.find("plugin process launch requires permission") != std::string::npos &&
             denied_prompts.size() == 1,
         "plugin tool dispatcher respects permission denial before process execution");

  auto const prompts_before_cancel = prompts.size();
  cancel_requested = true;
  auto canceled = dispatcher.dispatch(
      ava::agent::ProviderToolCall{.id = "call_canceled", .name = model_tool_name, .arguments_json = "{}"});
  expect(canceled && !canceled->success && canceled->payload.status == ava::agent::ToolResultStatus::Canceled &&
             canceled->result_text.find("canceled") != std::string::npos && prompts.size() == prompts_before_cancel,
         "plugin tool dispatcher reports semantic cancellation before permission or process execution");
}

void test_plugin_tool_dispatcher_rejects_invalid_result()
{
  auto const root = temp_root() / "plugin-tool-invalid-result";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const project_plugins = workspace / ".ava" / "plugins";
  auto const plugin_dir = project_plugins / "com.example.invalid";
  auto const state_file = root / "state" / "plugin-enablement.json";
  std::filesystem::create_directories(workspace);

  write_text(plugin_dir / "plugin.json", tool_manifest_json("com.example.invalid", "plugin.sh"));
  write_text(plugin_dir / "plugin.sh",
             "read line\n"
             "printf '%s\\n' '{\"id\":\"ava_1\",\"type\":\"initialized\",\"api_version\":\"ava."
             "plugin.v1\",\"plugin_version\":\"0.1.0\",\"contributions\":{}}'\n"
             "read line\n"
             "printf '%s\\n' '{\"id\":\"ava_tool_call_bad\",\"type\":\"tool.result\",\"ok\":true}'\n");
  auto enabled = ava::plugin::set_plugin_enabled(state_file, workspace, "com.example.invalid", true,
                                                 ava::plugin::PluginScope::Project);
  expect(enabled.has_value(), "invalid-result plugin test enables project plugin");

  ava::tools::ToolContext context;
  context.workspace_dir = workspace;
  context.plugin_global_plugins_dir = root / "global-plugins";
  context.plugin_project_plugins_dir = project_plugins;
  context.plugin_enablement_file = state_file;
  context.permission_resolver =
      [](ava::permissions::PermissionPrompt const&) -> ava::core::Result<ava::permissions::PermissionResolution> {
    return ava::permissions::PermissionResolution::Allow;
  };

  auto const model_tool_name = ava::plugin::plugin_model_tool_name("com.example.invalid", "todo_add");
  ava::agent::ToolDispatcher dispatcher(context);
  auto dispatched = dispatcher.dispatch(
      ava::agent::ProviderToolCall{.id = "call_bad", .name = model_tool_name, .arguments_json = "{}"});
  expect(dispatched && !dispatched->success && dispatched->result_text.find("malformed") != std::string::npos,
         "dispatcher reports malformed plugin tool results as tool errors");
}

void test_plugin_tool_registry_skips_name_collisions()
{
  auto const root = temp_root() / "plugin-tool-collisions";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const project_plugins = workspace / ".ava" / "plugins";
  auto const state_file = root / "state" / "plugin-enablement.json";
  std::filesystem::create_directories(workspace);

  write_text(project_plugins / "com.example.same" / "plugin.json", tool_manifest_json("com.example.same", "plugin.sh"));
  write_text(project_plugins / "com-example-same" / "plugin.json", tool_manifest_json("com-example-same", "plugin.sh"));
  auto enabled_first = ava::plugin::set_plugin_enabled(state_file, workspace, "com.example.same", true,
                                                       ava::plugin::PluginScope::Project);
  auto enabled_second = ava::plugin::set_plugin_enabled(state_file, workspace, "com-example-same", true,
                                                        ava::plugin::PluginScope::Project);
  expect(enabled_first.has_value() && enabled_second.has_value(), "collision plugin test enables both plugins");

  ava::tools::ToolContext context;
  context.workspace_dir = workspace;
  context.plugin_global_plugins_dir = root / "global-plugins";
  context.plugin_project_plugins_dir = project_plugins;
  context.plugin_enablement_file = state_file;

  auto const colliding_name = ava::plugin::plugin_model_tool_name("com.example.same", "todo_add");
  auto const schemas = ava::agent::ToolDispatcher::tool_schemas_json(context);
  auto const count = std::count_if(schemas.begin(), schemas.end(), [&](std::string const& schema) {
    return schema.find("\"name\":\"" + colliding_name + "\"") != std::string::npos;
  });
  expect(count == 1, "plugin registry exposes only one schema for colliding plugin tool names");
}

}  // namespace

void run_plugin_tests()
{
  test_plugin_manifest_parsing();
  test_plugin_discovery();
  test_plugin_enablement();
  test_plugin_protocol_helpers();
  test_plugin_stream_buffers();
  test_plugin_runner_initializes_and_shuts_down();
  test_plugin_runner_accepts_buffered_extra_records();
  test_plugin_runner_contained_failures();
  test_plugin_runner_tool_calls();
  test_plugin_runner_command_calls();
  test_plugin_runner_event_observation();
  test_enabled_plugin_event_hooks_observe_runtime_events();
  test_plugin_event_hook_failures_report_to_opt_in_sink();
  test_plugin_tool_dispatcher();
  test_plugin_tool_dispatcher_rejects_invalid_result();
  test_plugin_tool_registry_skips_name_collisions();
}
