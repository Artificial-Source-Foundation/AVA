#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>

#include "ava/core/json.h"
#include "ava/plugin/manifest.h"
#include "ava/plugin/runner.h"
#include "tests/support/test_harness.h"

namespace {

void write_text(std::filesystem::path const& path, std::string const& text)
{
  std::filesystem::create_directories(path.parent_path());
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file << text;
}

std::filesystem::path fresh_root(std::string const& name)
{
  auto const root = temp_root() / name;
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  std::filesystem::create_directories(root);
  return root;
}

std::string lifecycle_manifest_json(std::string id, std::string script_name)
{
  return std::string("{\n") +
         "  \"schema_version\": 1,\n"
         "  \"id\": \"" +
         ava::core::json::escape(id) +
         "\",\n"
         "  \"name\": \"Lifecycle Plugin\",\n"
         "  \"version\": \"0.1.0\",\n"
         "  \"api_version\": \"ava.plugin.v1\",\n"
         "  \"entrypoint\": {\"command\": \"/bin/sh\", \"args\": [\"" +
         ava::core::json::escape(script_name) +
         "\"]},\n"
         "  \"capabilities\": [\"tools\"],\n"
         "  \"contributes\": {\"tools\": [], \"commands\": []}\n"
         "}";
}

ava::plugin::PluginManifest lifecycle_manifest(std::filesystem::path const& plugin_dir, std::string id,
                                               std::string script_name)
{
  auto parsed = ava::plugin::parse_plugin_manifest(lifecycle_manifest_json(std::move(id), std::move(script_name)),
                                                   plugin_dir / "plugin.json");
  expect(parsed.has_value(),
         parsed ? "plugin lifecycle manifest parses" : "plugin lifecycle manifest parses: " + parsed.error().format());
  return parsed.value_or(ava::plugin::PluginManifest{});
}

ava::plugin::PluginRunnerOptions lifecycle_options(std::filesystem::path const& workspace,
                                                   std::chrono::milliseconds startup_timeout)
{
  ava::plugin::PluginRunnerOptions options;
  options.workspace_dir = workspace;
  options.startup_timeout = startup_timeout;
  options.request_timeout = std::chrono::milliseconds(500);
  options.max_record_bytes = 64 * 1024;
  options.max_stderr_bytes = 4096;
  return options;
}

void test_shutdown_kills_child_that_ignores_stdin_and_term()
{
  auto const root = fresh_root("plugin-runner-lifecycle-shutdown");
  auto const workspace = root / "workspace";
  auto const plugin_dir = root / "plugins" / "com.example.lifecycle";
  std::filesystem::create_directories(workspace);

  write_text(plugin_dir / "plugin.sh",
             "read line\n"
             "printf '%s\\n' '{\"id\":\"ava_1\",\"type\":\"initialized\",\"api_version\":\"ava."
             "plugin.v1\",\"plugin_version\":\"0.1.0\",\"contributions\":{\"tools\":[],\"commands\":[]}}'\n"
             "trap '' TERM\n"
             "while :; do sleep 1; done\n");

  auto process = ava::plugin::PluginProcess::start(lifecycle_manifest(plugin_dir, "com.example.lifecycle", "plugin.sh"),
                                                   lifecycle_options(workspace, std::chrono::milliseconds(500)));
  expect(process.has_value(), process ? "plugin lifecycle starts child for shutdown test"
                                      : "plugin lifecycle starts child for shutdown test: " + process.error().format());
  if (!process) return;

  auto const started = std::chrono::steady_clock::now();
  auto shutdown = (*process)->shutdown(std::chrono::milliseconds(20));
  auto const elapsed = std::chrono::steady_clock::now() - started;
  expect(shutdown.has_value(),
         shutdown ? "plugin lifecycle shutdown force-terminates unresponsive child"
                  : "plugin lifecycle shutdown force-terminates unresponsive child: " + shutdown.error().format());
  expect(elapsed < std::chrono::seconds(2), "plugin lifecycle shutdown does not wait on an ignored stdin close");
}

void test_start_reports_child_exit_and_stderr_during_initialize()
{
  auto const root = fresh_root("plugin-runner-lifecycle-exit");
  auto const workspace = root / "workspace";
  auto const plugin_dir = root / "plugins" / "com.example.exit";
  std::filesystem::create_directories(workspace);

  write_text(plugin_dir / "plugin.sh",
             "read line\n"
             "printf '%s\\n' 'lifecycle startup failed' >&2\n"
             "exit 9\n");

  auto started = ava::plugin::PluginProcess::start(lifecycle_manifest(plugin_dir, "com.example.exit", "plugin.sh"),
                                                   lifecycle_options(workspace, std::chrono::milliseconds(500)));
  auto const formatted = started ? std::string{} : started.error().format();
  expect(!started && formatted.find("exit 9") != std::string::npos &&
             formatted.find("lifecycle startup failed") != std::string::npos,
         "plugin lifecycle reports child exit status and stderr during initialize: " + formatted);
}

void test_startup_cancellation_terminates_hung_child()
{
  auto const root = fresh_root("plugin-runner-lifecycle-cancel");
  auto const workspace = root / "workspace";
  auto const plugin_dir = root / "plugins" / "com.example.cancel";
  std::filesystem::create_directories(workspace);

  write_text(plugin_dir / "plugin.sh", "sleep 10\n");

  int checks = 0;
  auto const started_at = std::chrono::steady_clock::now();
  auto started = ava::plugin::PluginProcess::start(lifecycle_manifest(plugin_dir, "com.example.cancel", "plugin.sh"),
                                                   lifecycle_options(workspace, std::chrono::seconds(5)),
                                                   [&checks] { return ++checks > 1; });
  auto const elapsed = std::chrono::steady_clock::now() - started_at;
  expect(!started && started.error().message().find("canceled") != std::string::npos,
         "plugin lifecycle honors startup cancellation");
  expect(elapsed < std::chrono::seconds(2), "plugin lifecycle cancellation avoids waiting for startup timeout");
}

}  // namespace

void run_plugin_runner_lifecycle_tests()
{
  test_shutdown_kills_child_that_ignores_stdin_and_term();
  test_start_reports_child_exit_and_stderr_during_initialize();
  test_startup_cancellation_terminates_hung_child();
}
