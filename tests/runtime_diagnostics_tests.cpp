#include "sys.h"
#include "tests/support/app_runtime_support.h"
#include "tests/support/fake_transport.h"
#include "tests/support/test_harness.h"
#include "ava/diagnostics/artifact_store.h"
#include "ava/diagnostics/runtime_diagnostics.h"
#include "ava/app/runtime.h"
#include "ava/provider/openai_provider.h"
#include "ava/core/json.h"

#include <array>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>
#include "debug.h"

namespace {

using namespace ava::tests;

std::filesystem::path diagnostics_root(std::string_view name)
{
  static std::atomic<unsigned long long> sequence{0};
  auto const root = temp_root() / (std::string(name) + "_" + std::to_string(static_cast<unsigned long long>(::getpid())) + "_" +
                                   std::to_string(sequence.fetch_add(1, std::memory_order_relaxed)));
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  static_cast<void>(::chmod(root.c_str(), 0700));
  return root;
}

ava::config::XdgPaths private_paths(std::filesystem::path const& root)
{
  ava::config::XdgPaths paths;
  paths.state_home = root / "state";
  paths.ava_state_dir = paths.state_home / "ava";
  paths.sessions_dir = paths.ava_state_dir / "sessions";
  return paths;
}

std::string read_all(std::filesystem::path const& path)
{
  std::ifstream input(path, std::ios::binary);
  return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

std::vector<std::string> lines(std::string const& text)
{
  std::vector<std::string> result;
  std::istringstream input(text);
  for (std::string line; std::getline(input, line);) result.push_back(std::move(line));
  return result;
}

void make_private_directories(std::initializer_list<std::filesystem::path> directories)
{
  for (auto const& directory : directories)
  {
    std::filesystem::create_directories(directory);
    static_cast<void>(::chmod(directory.c_str(), 0700));
  }
}

void test_disabled_runtime_diagnostics_is_artifact_free()
{
  auto const root = diagnostics_root("runtime-diagnostics-disabled");
  auto const paths = private_paths(root);
  {
    ava::diagnostics::RuntimeDiagnostics diagnostics(paths);
    expect(!diagnostics.trace_enabled() && !diagnostics.observation() && !diagnostics.trace_path(),
           "disabled runtime diagnostics has no production observation");
  }
  expect(!std::filesystem::exists(paths.state_home), "disabled runtime diagnostics performs no filesystem access");
  std::filesystem::remove_all(root);
}

void test_private_trace_sanitization_limits_and_counters()
{
  constexpr std::string_view session_canary = "SESSION_CANARY_PRIVATE_4091";
  constexpr std::string_view provider_canary = "PROVIDER_CANARY_PRIVATE_4092";
  constexpr std::string_view tool_canary = "TOOL_CANARY_PRIVATE_4093";
  constexpr std::string_view path_canary = "/tmp/PATH_CANARY_PRIVATE_4094";
  constexpr std::string_view command_canary = "rm -rf COMMAND_CANARY_PRIVATE_4095";
  constexpr std::string_view url_canary = "https://URL_CANARY_PRIVATE_4096.invalid";
  constexpr std::string_view secret_canary = "SECRET_CANARY_PRIVATE_4097";

  auto const root = diagnostics_root("runtime-diagnostics-trace");
  auto const paths = private_paths(root);
  auto created = ava::diagnostics::RuntimeDiagnostics::create(paths, true);
  expect(created && (*created)->trace_enabled() && (*created)->trace_path(), "explicit tracing securely initializes a production observation");
  if (!created)
    return;
  auto const trace_path_value = (*created)->trace_path();
  if (!trace_path_value)
    return;
  auto const trace_path = *trace_path_value;
  auto observation = (*created)->observation();
  ava::observability::TraceContext first{.run_id = "run-source-canary",
                                         .turn_id = "turn-source-canary",
                                         .session_id = std::string(session_canary),
                                         .provider_id = std::string(provider_canary),
                                         .parent_run_id = "parent-run-source-canary",
                                         .parent_turn_id = "parent-turn-source-canary",
                                         .parent_session_id = "parent-session-source-canary"};
  observation->emit(ava::observability::TraceEventType::TransportRequestResult, first, [&](auto& event) {
    event.call_id = std::string(tool_canary);
    event.phase = ava::observability::TracePhase::Transport;
    event.outcome = ava::observability::TraceOutcome::Success;
    event.fields = {{.key = "request_bytes", .value = "42"},
                    {.key = "streaming", .value = "true"},
                    {.key = "path", .value = std::string(path_canary), .provenance = ava::observability::FieldProvenance::Path},
                    {.key = "command", .value = std::string(command_canary)},
                    {.key = "url", .value = std::string(url_canary)},
                    {.key = "secret", .value = std::string(secret_canary), .provenance = ava::observability::FieldProvenance::Secret},
                    {.key = "status_code", .value = "200-not-safe"}};
  });
  observation->emit(ava::observability::TraceEventType::TransportRequestResult, first, [&](auto& event) {
    event.call_id = std::string(tool_canary);
    event.phase = ava::observability::TracePhase::Transport;
    event.outcome = ava::observability::TraceOutcome::Error;
    event.fields = {{.key = "status_code", .value = "503"}};
  });
  auto second = first;
  second.run_id = "different-run-source";
  observation->emit(ava::observability::TraceEventType::TransportRequestResult, second);
  for (std::size_t index = 0; index < 10'005; ++index) observation->emit(ava::observability::TraceEventType::AgentRunStart, first);

  (*created)->close();
  auto const trace = read_all(trace_path);
  auto const records = lines(trace);
  bool const canaries_absent = trace.find(session_canary) == std::string::npos && trace.find(provider_canary) == std::string::npos &&
                               trace.find(tool_canary) == std::string::npos && trace.find(path_canary) == std::string::npos &&
                               trace.find(command_canary) == std::string::npos && trace.find(url_canary) == std::string::npos &&
                               trace.find(secret_canary) == std::string::npos && trace.find("run-source-canary") == std::string::npos;
  expect(canaries_absent, "production trace omits raw identities, paths, commands, URLs, secrets, and tool identifiers");
  expect(records.size() <= 10'000 && trace.size() <= 10U * 1024U * 1024U, "production trace enforces the existing event and byte limits");
  expect(records.size() >= 3 && trace.find("\"request_bytes\":\"42\"") != std::string::npos && trace.find("\"streaming\":\"true\"") != std::string::npos &&
             trace.find("200-not-safe") == std::string::npos,
         "production trace retains only allowlisted proven numeric and boolean metadata");
  if (records.size() >= 3)
  {
    auto const first_run = ava::core::json::string_field(records[0], "run_id");
    auto const repeated_run = ava::core::json::string_field(records[1], "run_id");
    auto const different_run = ava::core::json::string_field(records[2], "run_id");
    auto const first_session = ava::core::json::string_field(records[0], "session_id");
    auto const repeated_session = ava::core::json::string_field(records[1], "session_id");
    expect(first_run && repeated_run && different_run && *first_run == *repeated_run && *first_run != *different_run && *first_run != "run-source-canary",
           "production identity aliases are stable per source and differ across sources");
    expect(first_session && repeated_session && *first_session == *repeated_session && *first_session != session_canary,
           "session aliases are stable and opaque");
  }

  auto const snapshot = ava::diagnostics::read_trace_counter_snapshot(paths);
  expect(snapshot.state == ava::diagnostics::StoredRecordState::Present && snapshot.record && snapshot.record->runtime_starts == 10'005 &&
             snapshot.record->provider_requests == 3 && snapshot.record->provider_failures == 1,
         "trace close drains first and persists one typed class/outcome counter snapshot");
  struct stat trace_metadata{};
  expect(::stat(trace_path.c_str(), &trace_metadata) == 0 && S_ISREG(trace_metadata.st_mode) && (trace_metadata.st_mode & 07777) == 0600 &&
             trace_metadata.st_nlink == 1 && trace_metadata.st_uid == ::geteuid(),
         "production trace is one owner-only singly-linked regular file");
  std::filesystem::remove_all(root);
}

void test_concurrent_traces_are_distinct()
{
  auto const root = diagnostics_root("runtime-diagnostics-concurrent");
  auto const paths = private_paths(root);
  std::array<std::shared_ptr<ava::diagnostics::RuntimeDiagnostics>, 2> diagnostics;
  std::array<bool, 2> succeeded{};
  std::vector<std::thread> threads;
  for (std::size_t index = 0; index < diagnostics.size(); ++index)
  {
    threads.emplace_back([&, index] {
      auto created = ava::diagnostics::RuntimeDiagnostics::create(paths, true);
      succeeded[index] = created.has_value();
      if (created)
        diagnostics[index] = std::move(*created);
    });
  }
  for (auto& thread : threads) thread.join();
  auto first = diagnostics[0] ? diagnostics[0]->trace_path() : std::nullopt;
  auto second = diagnostics[1] ? diagnostics[1]->trace_path() : std::nullopt;
  expect(succeeded[0] && succeeded[1] && first && second && *first != *second, "concurrent traced runtimes receive independent opaque files");
  for (auto& item : diagnostics)
    if (item)
      item->close();
  std::filesystem::remove_all(root);
}

void test_trace_initialization_rejects_unsafe_paths_nonblocking()
{
  for (std::string const fixture : {"symlink", "fifo", "hardlink", "permissions"})
  {
    auto const root = diagnostics_root("runtime-diagnostics-unsafe-" + fixture);
    auto const paths = private_paths(root);
    auto const diagnostics_dir = paths.ava_state_dir / "diagnostics";
    make_private_directories({paths.state_home, paths.ava_state_dir, diagnostics_dir});
    auto const traces = diagnostics_dir / "traces";
    if (fixture == "symlink")
    {
      auto const target = root / "target";
      make_private_directories({target});
      std::filesystem::create_directory_symlink(target, traces);
    }
    else if (fixture == "fifo")
    {
      expect(::mkfifo(traces.c_str(), 0600) == 0, "unsafe trace FIFO fixture is created");
    }
    else if (fixture == "hardlink")
    {
      auto const target = root / "target-file";
      std::ofstream(target) << "fixture";
      static_cast<void>(::chmod(target.c_str(), 0600));
      std::filesystem::create_hard_link(target, traces);
    }
    else
    {
      std::filesystem::create_directory(traces);
      static_cast<void>(::chmod(traces.c_str(), 0755));
    }
    auto created = ava::diagnostics::RuntimeDiagnostics::create(paths, true);
    expect(!created, "explicit trace rejects unsafe " + fixture + " storage without following or blocking");
    std::filesystem::remove_all(root);
  }
}

void test_last_failure_is_typed_and_message_free()
{
  constexpr std::string_view canary = "RAW_FAILURE_CANARY_PRIVATE_5101";
  auto const root = diagnostics_root("runtime-diagnostics-last-failure");
  auto const paths = private_paths(root);
  ava::diagnostics::RuntimeDiagnostics diagnostics(paths);
  ava::core::Error provider_error(ava::core::ErrorCategory::Provider, std::string(canary));
  provider_error.with_context("url", std::string(canary));
  diagnostics.record_terminal_failure(ava::diagnostics::RuntimeFailureClass::Provider, provider_error);
  auto const record_path = paths.ava_state_dir / "diagnostics" / "last-failure-v1.json";
  auto const first_body = read_all(record_path);
  auto const first = ava::diagnostics::read_last_failure_record(paths);
  expect(first.state == ava::diagnostics::StoredRecordState::Present && first.record &&
             first.record->failure.component == ava::diagnostics::ComponentClass::Provider && first_body.find(canary) == std::string::npos,
         "last failure stores only the stable terminal class and ErrorCategory mapping");
  diagnostics.record_terminal_failure(ava::diagnostics::RuntimeFailureClass::Runtime,
                                      ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "user input must not replace the record"));
  expect(read_all(record_path) == first_body, "user-invalid input does not overwrite the latest terminal failure");
  std::filesystem::remove_all(root);
}

class CollectingObserver final : public ava::observability::RunObserver
{
 public:
  void on_event(ava::observability::TraceEvent const&) override
  {
    std::lock_guard lock(mutex_);
    ++events_;
  }
  [[nodiscard]] std::size_t events() const
  {
    std::lock_guard lock(mutex_);
    return events_;
  }
  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT

 private:
  mutable std::mutex mutex_;
  std::size_t events_ = 0;
};

ava::core::Result<ava::app::runtime::Session> open_diagnostic_session(std::filesystem::path const& root,
                                                                      std::shared_ptr<ava::diagnostics::RuntimeDiagnostics> diagnostics)
{
  auto const paths = app_test_paths(root);
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  ava::app::runtime::OpenOptions options;
  options.workspace_dir = workspace;
  options.current_dir = workspace;
  options.paths = paths;
  options.sessionless = true;
  options.diagnostics = std::move(diagnostics);
  return ava::app::open_runtime_session(options);
}

void test_runtime_failure_boundaries_and_observation_precedence()
{
  constexpr std::string_view provider_canary = "PROVIDER_BODY_CANARY_PRIVATE_6201";
  auto const provider = ava::provider::OpenAIProvider("https://api.example.test");

  auto const canceled_root = diagnostics_root("runtime-diagnostics-canceled");
  auto const canceled_paths = app_test_paths(canceled_root);
  auto canceled_diagnostics = ava::diagnostics::RuntimeDiagnostics::create(canceled_paths, false);
  auto canceled_session = canceled_diagnostics ? open_diagnostic_session(canceled_root, *canceled_diagnostics)
                                               : ava::core::Result<ava::app::runtime::Session>(std::unexpected(canceled_diagnostics.error()));
  ava::tests::FakeTransport canceled_transport({});
  ava::app::runtime::RunOptions canceled_options;
  canceled_options.access_token = "token";
  canceled_options.cancel_requested = [] { return true; };
  auto canceled = canceled_session ? ava::app::run_prompt(*canceled_session, "cancel", provider, canceled_transport, canceled_options)
                                   : ava::core::Result<ava::agent::AgentLoopResult>(std::unexpected(canceled_session.error()));
  expect(!canceled && ava::diagnostics::read_last_failure_record(canceled_paths).state == ava::diagnostics::StoredRecordState::Absent,
         "runtime cancellation does not create a last-failure artifact");

  auto const provider_root = diagnostics_root("runtime-diagnostics-provider-failure");
  auto const provider_paths = app_test_paths(provider_root);
  auto provider_diagnostics = ava::diagnostics::RuntimeDiagnostics::create(provider_paths, false);
  auto provider_session = provider_diagnostics ? open_diagnostic_session(provider_root, *provider_diagnostics)
                                               : ava::core::Result<ava::app::runtime::Session>(std::unexpected(provider_diagnostics.error()));
  ava::tests::FakeTransport provider_transport({ava::provider::HttpResponse{.status_code = 500, .headers = {}, .body = std::string(provider_canary)}});
  ava::app::runtime::RunOptions provider_options;
  provider_options.access_token = "token";
  auto provider_result = provider_session ? ava::app::run_prompt(*provider_session, "fail", provider, provider_transport, provider_options)
                                          : ava::core::Result<ava::agent::AgentLoopResult>(std::unexpected(provider_session.error()));
  auto provider_failure = ava::diagnostics::read_last_failure_record(provider_paths);
  auto const provider_body = read_all(provider_paths.ava_state_dir / "diagnostics" / "last-failure-v1.json");
  expect(!provider_result && provider_failure.record && provider_failure.record->failure.component == ava::diagnostics::ComponentClass::Provider &&
             provider_body.find(provider_canary) == std::string::npos,
         "central runtime provider failure persists a strict message-free record");

  auto const tool_root = diagnostics_root("runtime-diagnostics-tool-failure");
  auto const tool_paths = app_test_paths(tool_root);
  auto tool_diagnostics = ava::diagnostics::RuntimeDiagnostics::create(tool_paths, true);
  auto tool_session = tool_diagnostics ? open_diagnostic_session(tool_root, *tool_diagnostics)
                                       : ava::core::Result<ava::app::runtime::Session>(std::unexpected(tool_diagnostics.error()));
  auto supplied_observer = std::make_shared<CollectingObserver>();
  ava::app::runtime::RunOptions tool_options;
  tool_options.access_token = "token";
  tool_options.observation = std::make_shared<ava::observability::RunObservation>(supplied_observer);
  tool_options.event_sink = [](ava::app::runtime::Event const&) -> ava::core::VoidResult {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Tool, "TOOL_FAILURE_CANARY_PRIVATE_6202"));
  };
  ava::tests::FakeTransport tool_transport({});
  auto tool_result = tool_session ? ava::app::run_prompt(*tool_session, "tool failure", provider, tool_transport, tool_options)
                                  : ava::core::Result<ava::agent::AgentLoopResult>(std::unexpected(tool_session.error()));
  auto tool_failure = ava::diagnostics::read_last_failure_record(tool_paths);
  expect(!tool_result && tool_failure.record && tool_failure.record->failure.component == ava::diagnostics::ComponentClass::Tool,
         "central runtime tool failure persists the typed tool terminal class");
  expect(supplied_observer->events() == 0, "enabled production diagnostics overrides an explicitly supplied programmatic observation");

  auto const session_root = diagnostics_root("runtime-diagnostics-session-failure");
  auto const session_paths = app_test_paths(session_root);
  auto session_diagnostics = ava::diagnostics::RuntimeDiagnostics::create(session_paths, false);
  auto failed_session = session_diagnostics ? open_diagnostic_session(session_root, *session_diagnostics)
                                            : ava::core::Result<ava::app::runtime::Session>(std::unexpected(session_diagnostics.error()));
  ava::app::runtime::RunOptions session_options;
  session_options.access_token = "token";
  session_options.event_sink = [](ava::app::runtime::Event const&) -> ava::core::VoidResult {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "SESSION_FAILURE_CANARY_PRIVATE_6203"));
  };
  ava::tests::FakeTransport session_transport({});
  auto session_result = failed_session ? ava::app::run_prompt(*failed_session, "session failure", provider, session_transport, session_options)
                                       : ava::core::Result<ava::agent::AgentLoopResult>(std::unexpected(failed_session.error()));
  auto session_failure = ava::diagnostics::read_last_failure_record(session_paths);
  expect(!session_result && session_failure.record && session_failure.record->failure.component == ava::diagnostics::ComponentClass::Session,
         "central runtime persistence failure persists the typed session terminal class");

  auto const preserved = read_all(provider_paths.ava_state_dir / "diagnostics" / "last-failure-v1.json");
  auto disabled_observer = std::make_shared<CollectingObserver>();
  provider_options.observation = std::make_shared<ava::observability::RunObservation>(disabled_observer);
  ava::tests::FakeTransport success_transport({sse_response(final_text_sse("ok"))});
  auto success = provider_session ? ava::app::run_prompt(*provider_session, "success", provider, success_transport, provider_options)
                                  : ava::core::Result<ava::agent::AgentLoopResult>(std::unexpected(provider_session.error()));
  expect(success && read_all(provider_paths.ava_state_dir / "diagnostics" / "last-failure-v1.json") == preserved,
         "a subsequent successful runtime does not overwrite the latest failure");
  expect(disabled_observer->events() > 0, "disabled production diagnostics preserves an explicitly supplied programmatic observation");

  if (tool_diagnostics)
    (*tool_diagnostics)->close();
  std::filesystem::remove_all(canceled_root);
  std::filesystem::remove_all(provider_root);
  std::filesystem::remove_all(tool_root);
  std::filesystem::remove_all(session_root);
}

}  // namespace

void run_runtime_diagnostics_tests()
{
  test_disabled_runtime_diagnostics_is_artifact_free();
  test_private_trace_sanitization_limits_and_counters();
  test_concurrent_traces_are_distinct();
  test_trace_initialization_rejects_unsafe_paths_nonblocking();
  test_last_failure_is_typed_and_message_free();
  test_runtime_failure_boundaries_and_observation_precedence();
}
