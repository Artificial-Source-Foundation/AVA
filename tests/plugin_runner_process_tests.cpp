#include "sys.h"
#include "tests/support/test_harness.h"
#include "ava/process/environment.h"
#include "ava/process/scope.h"
#include "ava/process/supervisor.h"
#include "ava/plugin/discovery.h"
#include "ava/plugin/runner.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <iterator>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#ifndef AVA_FAKE_PLUGIN_CHILD_PATH
#define AVA_FAKE_PLUGIN_CHILD_PATH ""
#endif

namespace {

using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

struct TestAuthority
{
  std::shared_ptr<ava::process::Supervisor> supervisor;
  ava::process::ProcessScopeV1 application;
  ava::process::ProcessScopeV1 session;
  ava::process::ProcessScopeV1 run;
};

std::optional<TestAuthority> make_authority()
{
  auto supervisor = std::make_shared<ava::process::Supervisor>();
  auto application = ava::process::ProcessScopeV1::application(supervisor);
  if (!application)
  {
    expect(false, "plugin process test application scope: " + application.error().format());
    return std::nullopt;
  }
  auto session = application->session();
  if (!session)
  {
    expect(false, "plugin process test session scope: " + session.error().format());
    return std::nullopt;
  }
  auto run = session->run();
  if (!run)
  {
    expect(false, "plugin process test run scope: " + run.error().format());
    return std::nullopt;
  }
  return TestAuthority{.supervisor = std::move(supervisor), .application = std::move(*application), .session = std::move(*session), .run = std::move(*run)};
}

std::filesystem::path test_root(std::string_view label)
{
  auto root =
      std::filesystem::temp_directory_path() / ("ava-plugin-process-" + std::string(label) + "-" + std::to_string(Clock::now().time_since_epoch().count()));
  std::error_code error;
  std::filesystem::remove_all(root, error);
  std::filesystem::create_directories(root);
  return root;
}

void write_text(std::filesystem::path const& path, std::string_view text)
{
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output << text;
}

std::string read_text(std::filesystem::path const& path)
{
  std::ifstream input(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

bool wait_for_path(std::filesystem::path const& path, std::chrono::milliseconds timeout = 2s)
{
  auto const deadline = Clock::now() + timeout;
  while (Clock::now() < deadline)
  {
    if (std::filesystem::exists(path))
      return true;
    std::this_thread::sleep_for(10ms);
  }
  return std::filesystem::exists(path);
}

bool wait_for_process_absent(std::string_view identity)
{
  if (identity.empty())
    return false;
  auto const path = std::filesystem::path("/proc") / std::string(identity);
  auto const deadline = Clock::now() + 2s;
  while (Clock::now() < deadline)
  {
    if (!std::filesystem::exists(path))
      return true;
    std::this_thread::sleep_for(10ms);
  }
  return !std::filesystem::exists(path);
}

ava::plugin::PluginManifest fake_manifest(std::filesystem::path const& directory, std::string scenario, std::filesystem::path const& marker = {})
{
  std::filesystem::create_directories(directory);
  ava::plugin::PluginManifest manifest;
  manifest.schema_version = 1;
  manifest.id = "com.example.process";
  manifest.name = "Process fixture";
  manifest.version = "1.0.0";
  manifest.api_version = "ava.plugin.v1";
  manifest.entrypoint.command = AVA_FAKE_PLUGIN_CHILD_PATH;
  manifest.entrypoint.args.push_back(std::move(scenario));
  if (!marker.empty())
    manifest.entrypoint.args.push_back(marker.string());
  manifest.path = directory / "plugin.json";
  manifest.directory = directory;
  return manifest;
}

ava::plugin::PluginRunnerOptions options_for(std::filesystem::path const& workspace, ava::process::ProcessScopeV1 const& scope,
                                             std::chrono::milliseconds startup_timeout = 500ms, std::chrono::milliseconds request_timeout = 500ms)
{
  return ava::plugin::PluginRunnerOptions{.workspace_dir = workspace,
                                          .startup_timeout = startup_timeout,
                                          .request_timeout = request_timeout,
                                          .max_record_bytes = 64 * 1024,
                                          .max_stderr_bytes = 64 * 1024,
                                          .process_scope = scope};
}

std::vector<ava::process::ProcessSnapshotRecordV1> plugin_records(ava::process::Supervisor const& supervisor)
{
  auto snapshot = supervisor.snapshot();
  std::vector<ava::process::ProcessSnapshotRecordV1> records;
  for (auto const& record : snapshot.records)
  {
    if (record.role == ava::process::ProcessRoleV1::Plugin)
      records.push_back(record);
  }
  return records;
}

bool completely_settled(ava::process::ProcessSnapshotRecordV1 const& record)
{
  return record.state == ava::process::ProcessStateV1::Finished && record.cleanup == ava::process::CleanupStateV1::Complete && record.settlement_count == 1;
}

void finish_supervisor(TestAuthority& authority)
{
  authority.supervisor->stop_accepting();
  auto const result = authority.supervisor->shutdown(Clock::now() + 2s);
  expect(result.complete && result.incomplete_count == 0 && authority.supervisor->snapshot().live_records == 0,
         "plugin process test supervisor finishes with no live records");
}

class EnvironmentRestore final
{
 public:
  explicit EnvironmentRestore(std::vector<std::pair<std::string, std::string>> values)
  {
    for (auto const& [name, value] : values)
    {
      char const* prior = std::getenv(name.c_str());
      previous_.push_back({name, prior ? std::optional<std::string>(prior) : std::nullopt});
      ::setenv(name.c_str(), value.c_str(), 1);
    }
  }

  ~EnvironmentRestore()
  {
    for (auto const& [name, value] : previous_)
    {
      if (value)
        ::setenv(name.c_str(), value->c_str(), 1);
      else
        ::unsetenv(name.c_str());
    }
  }

 private:
  std::vector<std::pair<std::string, std::optional<std::string>>> previous_;
};

void test_compatibility_environment_bare_command_and_idempotence()
{
  auto authority = make_authority();
  if (!authority)
    return;
  auto const root = test_root("compatibility");
  auto const workspace = root / "workspace";
  auto const plugin_dir = root / "plugin";
  std::filesystem::create_directories(workspace);

  auto process = ava::plugin::PluginProcess::start(fake_manifest(plugin_dir, "normal"), options_for(workspace, authority->run));
  expect(process && (*process)->initialization().api_version == "ava.plugin.v1" && (*process)->initialization().plugin_version == "1.2.3",
         process ? "supervised plugin initialize remains protocol-compatible" : "supervised plugin initialize: " + process.error().format());
  if (process)
  {
    auto called = (*process)->call_tool("tool", "{}", "compat");
    expect(called && called->ok && called->content == "compatible", "supervised plugin call remains JSONL-compatible");
    auto first = (*process)->shutdown(500ms);
    auto second = (*process)->shutdown(500ms);
    expect(first && second, "plugin shutdown is successful and idempotent");
  }
  auto records = plugin_records(*authority->supervisor);
  expect(records.size() == 1 && completely_settled(records.front()), "compatibility plugin record settles exactly once with complete cleanup");

  EnvironmentRestore environment({{"HOME", "CANARY_PLUGIN_HOME"},
                                  {"HTTPS_PROXY", "CANARY_PLUGIN_PROXY"},
                                  {"OPENAI_API_KEY", "CANARY_PLUGIN_CREDENTIAL"},
                                  {"LD_PRELOAD", "CANARY_PLUGIN_LOADER"},
                                  {"SSH_AUTH_SOCK", "CANARY_PLUGIN_AGENT"},
                                  {"AVA_PRIVATE_TOKEN", "CANARY_PLUGIN_AVA"}});
  auto environment_authority = make_authority();
  if (!environment_authority)
    return;
  auto const environment_file = root / "environment.txt";
  auto environment_process =
      ava::plugin::PluginProcess::start(fake_manifest(plugin_dir, "environment", environment_file), options_for(workspace, environment_authority->run));
  expect(environment_process.has_value(),
         environment_process ? "plugin exact environment fixture initializes" : "plugin exact environment fixture: " + environment_process.error().format());
  if (environment_process)
  {
    auto called = (*environment_process)->call_tool("tool", "{}", "compat");
    auto shutdown = (*environment_process)->shutdown(500ms);
    expect(called && shutdown, "plugin exact environment fixture completes");
  }
  auto const child_environment = read_text(environment_file);
  auto const expected_environment =
      "PATH=" + std::string(ava::process::kTrustedEnvironmentPathV1) + "\nLANG=C.UTF-8\nLC_ALL=C.UTF-8\nPWD=" + plugin_dir.string() + "\n";
  expect(child_environment == expected_environment, "plugin receives only the exact minimal environment in canonical order");
  expect(std::string(std::getenv("HOME")) == "CANARY_PLUGIN_HOME" && std::string(std::getenv("HTTPS_PROXY")) == "CANARY_PLUGIN_PROXY" &&
             std::string(std::getenv("OPENAI_API_KEY")) == "CANARY_PLUGIN_CREDENTIAL" && std::string(std::getenv("LD_PRELOAD")) == "CANARY_PLUGIN_LOADER" &&
             std::string(std::getenv("SSH_AUTH_SOCK")) == "CANARY_PLUGIN_AGENT" && std::string(std::getenv("AVA_PRIVATE_TOKEN")) == "CANARY_PLUGIN_AVA",
         "plugin launch does not mutate its parent environment");
  finish_supervisor(*environment_authority);

  auto bare_authority = make_authority();
  if (!bare_authority)
    return;
  auto bare = fake_manifest(plugin_dir, "normal");
  bare.entrypoint.command = "sh";
  bare.entrypoint.args = {"-c",
                          "IFS= read -r init; printf '%s\\n' '{\"id\":\"ava_1\",\"type\":\"initialized\",\"api_version\":\"ava.plugin.v1\","
                          "\"plugin_version\":\"1.0\",\"contributions\":{}}'; IFS= read -r request; printf '%s\\n' "
                          "'{\"id\":\"ava_tool_compat\",\"type\":\"tool.result\",\"ok\":true,\"content\":\"bare\"}'; while IFS= read -r line; do :; done"};
  auto bare_process = ava::plugin::PluginProcess::start(std::move(bare), options_for(workspace, bare_authority->run));
  auto bare_call = bare_process ? (*bare_process)->call_tool("tool", "{}", "compat")
                                : ava::core::Result<ava::plugin::PluginToolCallResult>(std::unexpected(bare_process.error()));
  auto bare_shutdown = bare_process ? (*bare_process)->shutdown(500ms) : ava::core::VoidResult(std::unexpected(bare_process.error()));
  expect(bare_call && bare_call->content == "bare" && bare_shutdown, "bare plugin command resolves through the exact trusted PATH");
  finish_supervisor(*bare_authority);
  finish_supervisor(*authority);
}

void test_prelaunch_authority_cancel_and_discovery_are_process_free()
{
  auto authority = make_authority();
  if (!authority)
    return;
  auto const root = test_root("prelaunch");
  auto const workspace = root / "workspace";
  auto const plugin_dir = workspace / ".ava" / "plugins" / "com.example.discovery";
  auto const marker = root / "should-not-exist";
  std::filesystem::create_directories(workspace);
  write_text(plugin_dir / "plugin.json",
             "{\"schema_version\":1,\"id\":\"com.example.discovery\",\"name\":\"Discovery\",\"version\":\"1\","
             "\"api_version\":\"ava.plugin.v1\",\"entrypoint\":{\"command\":\"/bin/sh\",\"args\":[\"-c\",\"touch should-not-run\"]}}\n");
  auto discovered = ava::plugin::discover_plugins({.global_plugins_dir = root / "global", .project_plugins_dir = workspace / ".ava" / "plugins"});
  auto initial = authority->supervisor->snapshot();
  expect(discovered && discovered->size() == 1 && !initial.monitor_started && initial.live_records == 0 && initial.records.empty(),
         "plugin discovery and basic scope startup create no process or monitor");

  auto missing_options = options_for(workspace, authority->run);
  missing_options.process_scope.reset();
  auto missing = ava::plugin::PluginProcess::start(fake_manifest(root / "missing", "normal", marker), missing_options);
  auto after_missing = authority->supervisor->snapshot();
  expect(!missing && missing.error().message().find("authority is required") != std::string::npos && !after_missing.monitor_started &&
             after_missing.live_records == 0 && after_missing.records.empty() && !std::filesystem::exists(marker),
         "missing plugin scope fails before reservation and spawn");

  auto operation = authority->run.operation();
  expect(operation.has_value(), "plugin derivation-failure fixture creates an already-terminal owner");
  if (operation)
  {
    auto invalid_parent = ava::plugin::PluginProcess::start(fake_manifest(root / "invalid-parent", "normal", marker), options_for(workspace, *operation));
    auto after_invalid_parent = authority->supervisor->snapshot();
    expect(!invalid_parent && invalid_parent.error().message().find("derive plugin operation") != std::string::npos && !after_invalid_parent.monitor_started &&
               after_invalid_parent.live_records == 0 && after_invalid_parent.records.empty(),
           "plugin operation-scope derivation failure is actionable and precedes reservation");
  }

  auto canceled =
      ava::plugin::PluginProcess::start(fake_manifest(root / "canceled", "normal", marker), options_for(workspace, authority->run), [] { return true; });
  auto after_cancel = authority->supervisor->snapshot();
  expect(!canceled && canceled.error().message().find("canceled") != std::string::npos && !after_cancel.monitor_started && after_cancel.live_records == 0 &&
             after_cancel.records.empty() && !std::filesystem::exists(marker),
         "pre-canceled plugin startup creates no child or process record");
  finish_supervisor(*authority);
}

void test_failure_reason_mapping_and_output_accounting()
{
  auto run_case = [](std::string_view label, std::string scenario, std::chrono::milliseconds startup_timeout, std::chrono::milliseconds request_timeout,
                     std::size_t record_limit, std::size_t stderr_limit, ava::process::TerminationReasonV1 expected_reason, bool call_after_start,
                     bool cancel_call, bool expect_start_success) {
    auto authority = make_authority();
    if (!authority)
      return;
    auto const root = test_root(label);
    auto const workspace = root / "workspace";
    auto const marker = root / "marker";
    std::filesystem::create_directories(workspace);
    auto options = options_for(workspace, authority->run, startup_timeout, request_timeout);
    options.max_record_bytes = record_limit;
    options.max_stderr_bytes = stderr_limit;
    auto process = ava::plugin::PluginProcess::start(fake_manifest(root / "plugin", std::move(scenario), marker), options);
    expect(static_cast<bool>(process) == expect_start_success, std::string(label) + " reaches its expected startup boundary");
    if (process && call_after_start)
    {
      auto called = (*process)->call_tool(
          "tool", "{}", "compat", cancel_call ? ava::plugin::CancelCallback([marker] { return wait_for_path(marker, 10ms); }) : ava::plugin::CancelCallback{});
      expect(!called, std::string(label) + " returns a bounded call failure");
      auto shutdown = (*process)->shutdown(100ms);
      expect(shutdown.has_value(), std::string(label) + " failure cleanup remains explicitly observable as complete");
    }
    auto records = plugin_records(*authority->supervisor);
    expect(records.size() == 1 && completely_settled(records.front()) && records.front().reason == expected_reason,
           std::string(label) + " commits the expected first termination reason and settles once");
    if (label == "oversized-stdout")
      expect(records.front().stdout_truncated && records.front().stdout_bytes >= 8192, "oversized plugin stdout is fully accounted and marked truncated");
    finish_supervisor(*authority);
  };

  run_case("startup-timeout", "startup-hang", 50ms, 500ms, 64 * 1024, 64 * 1024, ava::process::TerminationReasonV1::DeadlineExpired, false, false, false);
  run_case("request-timeout", "request-hang", 500ms, 50ms, 64 * 1024, 64 * 1024, ava::process::TerminationReasonV1::DeadlineExpired, true, false, true);
  run_case("request-cancel", "request-hang", 500ms, 2s, 64 * 1024, 64 * 1024, ava::process::TerminationReasonV1::Canceled, true, true, true);
  run_case("malformed-framing", "malformed", 500ms, 500ms, 64 * 1024, 64 * 1024, ava::process::TerminationReasonV1::ProtocolFailure, true, false, true);
  run_case("oversized-stdout", "oversized-initialize", 500ms, 500ms, 64, 64 * 1024, ava::process::TerminationReasonV1::OutputLimit, false, false, false);

  auto stderr_authority = make_authority();
  if (!stderr_authority)
    return;
  auto const root = test_root("oversized-stderr");
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  auto options = options_for(workspace, stderr_authority->run);
  options.max_stderr_bytes = 64;
  auto process = ava::plugin::PluginProcess::start(fake_manifest(root / "plugin", "stderr-large"), options);
  expect(process && (*process)->stderr_truncated() && (*process)->stderr_tail().size() == 64, "oversized plugin stderr keeps only its bounded tail");
  if (process)
  {
    auto shutdown = (*process)->shutdown(500ms);
    expect(shutdown.has_value(), "stderr truncation does not change protocol success");
  }
  auto records = plugin_records(*stderr_authority->supervisor);
  expect(records.size() == 1 && completely_settled(records.front()) && records.front().stderr_truncated && records.front().stderr_bytes >= 8192,
         "every stderr byte is supervisor-accounted and truncation is recorded");
  finish_supervisor(*stderr_authority);
}

void test_eof_term_refusal_and_natural_descendant_cleanup()
{
  auto eof_authority = make_authority();
  if (!eof_authority)
    return;
  auto const eof_root = test_root("endpoint-eof");
  std::filesystem::create_directories(eof_root / "workspace");
  auto eof_process =
      ava::plugin::PluginProcess::start(fake_manifest(eof_root / "plugin", "endpoint-eof"), options_for(eof_root / "workspace", eof_authority->run));
  expect(eof_process.has_value(), "endpoint EOF fixture initializes before leader exit");
  if (eof_process)
  {
    auto called = (*eof_process)->call_tool("tool", "{}", "compat");
    auto shutdown = (*eof_process)->shutdown();
    expect(!called && shutdown, "endpoint EOF is reported without losing complete cleanup");
  }
  auto eof_records = plugin_records(*eof_authority->supervisor);
  expect(eof_records.size() == 1 && completely_settled(eof_records.front()) && eof_records.front().reason == ava::process::TerminationReasonV1::NaturalExit,
         "natural leader EOF wins the first-reason race when already observed");
  finish_supervisor(*eof_authority);

  auto refusal_authority = make_authority();
  if (!refusal_authority)
    return;
  auto const refusal_root = test_root("term-refusal");
  std::filesystem::create_directories(refusal_root / "workspace");
  auto refusal_process = ava::plugin::PluginProcess::start(fake_manifest(refusal_root / "plugin", "shutdown-term-refusal"),
                                                           options_for(refusal_root / "workspace", refusal_authority->run));
  auto const started = Clock::now();
  auto refusal_shutdown = refusal_process ? (*refusal_process)->shutdown(250ms) : ava::core::VoidResult(std::unexpected(refusal_process.error()));
  auto const elapsed = Clock::now() - started;
  expect(refusal_shutdown && elapsed < 3s, "plugin shutdown escalates TERM refusal within one bounded cleanup deadline");
  auto refusal_records = plugin_records(*refusal_authority->supervisor);
  expect(refusal_records.size() == 1 && completely_settled(refusal_records.front()) &&
             refusal_records.front().reason == ava::process::TerminationReasonV1::OwnerShutdown,
         "explicit TERM-refusal cleanup is owner shutdown with complete settlement");
  finish_supervisor(*refusal_authority);

  auto descendant_authority = make_authority();
  if (!descendant_authority)
    return;
  auto const descendant_root = test_root("leader-first-descendant");
  auto const descendant_marker = descendant_root / "descendant";
  std::filesystem::create_directories(descendant_root / "workspace");
  auto descendant_process = ava::plugin::PluginProcess::start(fake_manifest(descendant_root / "plugin", "descendant", descendant_marker),
                                                              options_for(descendant_root / "workspace", descendant_authority->run, 500ms, 2s));
  expect(descendant_process.has_value(), "leader-first descendant fixture initializes");
  if (descendant_process)
  {
    auto called = (*descendant_process)->call_tool("tool", "{}", "compat");
    auto shutdown = (*descendant_process)->shutdown();
    expect(!called && shutdown, "natural leader-first exit remains a bounded protocol failure to the caller");
  }
  auto const descendant_identity = read_text(descendant_marker);
  auto const trimmed_identity = descendant_identity.empty() ? std::string{} : descendant_identity.substr(0, descendant_identity.find('\n'));
  auto descendant_records = plugin_records(*descendant_authority->supervisor);
  expect(descendant_records.size() == 1 && completely_settled(descendant_records.front()) &&
             descendant_records.front().reason == ava::process::TerminationReasonV1::NaturalExit && wait_for_process_absent(trimmed_identity),
         "Supervisor immediately cleans a same-group TERM-refusing descendant after natural leader exit");
  finish_supervisor(*descendant_authority);
}

void test_owner_prefix_isolation_for_concurrent_operations()
{
  auto authority = make_authority();
  if (!authority)
    return;
  auto run_a = authority->session.run();
  auto run_b = authority->session.run();
  if (!run_a || !run_b)
  {
    expect(false, "concurrent plugin run scopes derive");
    return;
  }
  auto const root = test_root("owner-isolation");
  auto const workspace = root / "workspace";
  auto const marker_a = root / "a.started";
  auto const marker_b = root / "b.started";
  std::filesystem::create_directories(workspace);
  auto process_a = ava::plugin::PluginProcess::start(fake_manifest(root / "plugin-a", "request-hang", marker_a), options_for(workspace, *run_a, 500ms, 5s));
  auto process_b = ava::plugin::PluginProcess::start(fake_manifest(root / "plugin-b", "request-hang", marker_b), options_for(workspace, *run_b, 500ms, 5s));
  expect(process_a && process_b, "concurrent plugin operations initialize under distinct run prefixes");
  if (!process_a || !process_b)
    return;

  auto call_a = std::async(std::launch::async, [&] { return (*process_a)->call_tool("tool", "{}", "compat"); });
  auto call_b = std::async(std::launch::async, [&] { return (*process_b)->call_tool("tool", "{}", "compat"); });
  expect(wait_for_path(marker_a) && wait_for_path(marker_b), "both concurrent plugin operations publish their request boundary");

  auto stopped_a = authority->supervisor->request_stop(run_a->owner_prefix(), ava::process::TerminationReasonV1::OwnerShutdown, Clock::now() + 2s);
  expect(stopped_a && stopped_a->matched == 1, "one run-prefix stop matches only its plugin operation");
  expect(call_a.wait_for(2s) == std::future_status::ready, "stopped plugin operation settles promptly");
  auto mid_snapshot = authority->supervisor->snapshot();
  auto const running_b = std::ranges::any_of(mid_snapshot.records, [](auto const& record) {
    return record.role == ava::process::ProcessRoleV1::Plugin && record.state != ava::process::ProcessStateV1::Finished;
  });
  expect(running_b && call_b.wait_for(50ms) == std::future_status::timeout, "sibling plugin operation remains live after isolated prefix stop");

  auto stopped_b = authority->supervisor->request_stop(run_b->owner_prefix(), ava::process::TerminationReasonV1::OwnerShutdown, Clock::now() + 2s);
  expect(stopped_b && stopped_b->matched == 1 && call_b.wait_for(2s) == std::future_status::ready, "second prefix independently settles its operation");
  static_cast<void>(call_a.get());
  static_cast<void>(call_b.get());
  auto shutdown_a = (*process_a)->shutdown();
  auto shutdown_b = (*process_b)->shutdown();
  auto records = plugin_records(*authority->supervisor);
  expect(shutdown_a && shutdown_b && records.size() == 2 && std::ranges::all_of(records, completely_settled) &&
             std::ranges::all_of(records, [](auto const& record) { return record.reason == ava::process::TerminationReasonV1::OwnerShutdown; }) &&
             authority->supervisor->snapshot().live_records == 0,
         "concurrent plugin operations finish once with complete isolated cleanup");
  finish_supervisor(*authority);
}

}  // namespace

void run_plugin_runner_process_tests()
{
  if (ava::process::platform_support_v1() != ava::process::PlatformSupportV1::Posix)
  {
    expect(true, "plugin Supervisor runner is intentionally unsupported on this platform");
    return;
  }
  expect(std::filesystem::is_regular_file(AVA_FAKE_PLUGIN_CHILD_PATH), "plugin process fake fixture is available");
  test_compatibility_environment_bare_command_and_idempotence();
  test_prelaunch_authority_cancel_and_discovery_are_process_free();
  test_failure_reason_mapping_and_output_accounting();
  test_eof_term_refusal_and_natural_descendant_cleanup();
  test_owner_prefix_isolation_for_concurrent_operations();
}
