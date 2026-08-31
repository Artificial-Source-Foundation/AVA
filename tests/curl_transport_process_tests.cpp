#include "sys.h"
#include "tests/support/test_harness.h"
#include "ava/http/curl_transport.h"
#include "ava/http/curl_transport_test_support.h"
#include "ava/process/scope.h"
#include "ava/process/supervisor.h"
#include "ava/process/supervisor_test_support.h"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef AVA_FAKE_CURL_CHILD_PATH
#define AVA_FAKE_CURL_CHILD_PATH ""
#endif

namespace {

using namespace std::chrono_literals;

ava::process::ProcessScopeV1 require_application_scope(std::shared_ptr<ava::process::Supervisor> const& supervisor)
{
  auto scope = ava::process::ProcessScopeV1::application(supervisor);
  if (!scope)
    throw std::runtime_error(scope.error().format());
  return *scope;
}

class CurlFixture final
{
 public:
  CurlFixture() : supervisor(std::make_shared<ava::process::Supervisor>()), scope(require_application_scope(supervisor)), transport(scope)
  {
    auto installed = ava::http::testing::CurlTransportTestAccess::set_executable(transport, AVA_FAKE_CURL_CHILD_PATH);
    if (!installed)
      throw std::runtime_error(installed.error().format());
  }

  [[nodiscard]] ava::process::ProcessSnapshotRecordV1 const* only_record() const
  {
    auto const& records = snapshot.records;
    if (records.size() != 1)
      return nullptr;
    return &records.front();
  }

  void capture_snapshot() { snapshot = supervisor->snapshot(); }

  std::shared_ptr<ava::process::Supervisor> supervisor;
  ava::process::ProcessScopeV1 scope;
  ava::http::CurlCliTransport transport;
  ava::process::ProcessSnapshotV1 snapshot;
};

class EnvironmentRestore final
{
 public:
  explicit EnvironmentRestore(std::vector<std::string> names)
  {
    for (auto& name : names)
    {
      auto const* value = std::getenv(name.c_str());
      values_.push_back({std::move(name), value == nullptr ? std::nullopt : std::optional<std::string>(value)});
    }
  }
  ~EnvironmentRestore()
  {
    for (auto const& [name, value] : values_)
    {
      if (value)
        static_cast<void>(::setenv(name.c_str(), value->c_str(), 1));
      else
        static_cast<void>(::unsetenv(name.c_str()));
    }
  }

 private:
  std::vector<std::pair<std::string, std::optional<std::string>>> values_;
};

ava::http::HttpRequest request_for(std::string_view path, int timeout_ms = 3000)
{
  return {.method = "GET",
          .url = "https://curl.test" + std::string(path),
          .headers = {},
          .body = {},
          .timeout_ms = timeout_ms,
          .follow_redirects = false,
          .include_response_headers = false,
          .resolve_hosts = {}};
}

bool settled_record(ava::process::ProcessSnapshotRecordV1 const* record, ava::process::TerminationReasonV1 reason)
{
  return record != nullptr && record->role == ava::process::ProcessRoleV1::Curl && record->state == ava::process::ProcessStateV1::Finished &&
         record->reason == reason && record->cleanup == ava::process::CleanupStateV1::Complete && record->settlement_count == 1;
}

bool no_waitable_immediate_child()
{
  errno = 0;
  int status = 0;
  auto const child = ::waitpid(-1, &status, WNOHANG);
  return child == -1 && errno == ECHILD;
}

void test_exact_argv_config_body_and_success()
{
  CurlFixture fixture;
  auto request = request_for("/success");
  request.method = "PUT";
  request.headers = {{"Accept", "application/json"}, {"X-Test", "header-value"}};
  request.body = "fixed request body";
  request.follow_redirects = true;
  request.include_response_headers = true;
  request.resolve_hosts = {"curl.test:443:127.0.0.1"};
  auto response = fixture.transport.send(request);
  fixture.capture_snapshot();
  auto const* record = fixture.only_record();
  expect(response && response->status_code == 200 && response->headers["X-Fake"] == "yes" && response->body.find("argv=fixed") != std::string::npos &&
             response->body.find("streaming=false") != std::string::npos && response->body.find("request = \"PUT\"") != std::string::npos &&
             response->body.find("header = \"X-Test: header-value\"") != std::string::npos &&
             response->body.find("resolve = \"curl.test:443:127.0.0.1\"") != std::string::npos &&
             response->body.find("body=fixed request body") != std::string::npos,
         "supervised curl preserves fixed argv and parent-generated config/body/status parsing");
  expect(settled_record(record, ava::process::TerminationReasonV1::NaturalExit) && record->stdout_bytes > response->body.size() && !record->stdout_truncated &&
             !record->stderr_truncated && fixture.snapshot.live_records == 0,
         "successful curl request has one exactly-once finished managed-group record with raw output accounting");
}

void test_exact_environment_capture()
{
  EnvironmentRestore restore(
      {"HTTPS_PROXY", "CURL_CA_BUNDLE", "OPENAI_API_KEY", "AWS_SECRET_ACCESS_KEY", "LD_PRELOAD", "GIT_ASKPASS", "SSH_AUTH_SOCK", "AVA_CURL_FORBIDDEN_CANARY"});
  static_cast<void>(::setenv("HTTPS_PROXY", "https://captured-proxy.invalid", 1));
  static_cast<void>(::setenv("CURL_CA_BUNDLE", "/captured/ca.pem", 1));
  static_cast<void>(::setenv("OPENAI_API_KEY", "PROVIDER_SECRET_CANARY", 1));
  static_cast<void>(::setenv("AWS_SECRET_ACCESS_KEY", "CLOUD_SECRET_CANARY", 1));
  static_cast<void>(::setenv("LD_PRELOAD", "/loader/canary.so", 1));
  static_cast<void>(::setenv("GIT_ASKPASS", "/askpass/canary", 1));
  static_cast<void>(::setenv("SSH_AUTH_SOCK", "/agent/canary", 1));
  static_cast<void>(::setenv("AVA_CURL_FORBIDDEN_CANARY", "ARBITRARY_AVA_CANARY", 1));

  CurlFixture fixture;
  static_cast<void>(::setenv("HTTPS_PROXY", "https://mutated-proxy.invalid", 1));
  static_cast<void>(::setenv("CURL_CA_BUNDLE", "/mutated/ca.pem", 1));
  auto response = fixture.transport.send(request_for("/environment"));
  fixture.capture_snapshot();
  expect(response && response->body.find("HTTPS_PROXY=https://captured-proxy.invalid") != std::string::npos &&
             response->body.find("CURL_CA_BUNDLE=/captured/ca.pem") != std::string::npos && response->body.find("mutated-proxy") == std::string::npos &&
             response->body.find("/mutated/ca.pem") == std::string::npos && response->body.find("OPENAI_API_KEY") == std::string::npos &&
             response->body.find("AWS_SECRET_ACCESS_KEY") == std::string::npos && response->body.find("LD_PRELOAD") == std::string::npos &&
             response->body.find("GIT_ASKPASS") == std::string::npos && response->body.find("SSH_AUTH_SOCK") == std::string::npos &&
             response->body.find("AVA_CURL_FORBIDDEN_CANARY") == std::string::npos,
         "ava-curl-v1 inherits only captured proxy/CA inputs and excludes provider, cloud, loader, askpass, agent, and arbitrary AVA values");
  expect(settled_record(fixture.only_record(), ava::process::TerminationReasonV1::NaturalExit),
         "environment inspection request remains supervised and exactly once settled");
}

void test_stream_separation_hup_and_callbacks()
{
  {
    CurlFixture fixture;
    auto response = fixture.transport.send(request_for("/separate"));
    fixture.capture_snapshot();
    auto const* record = fixture.only_record();
    expect(response && response->status_code == 201 && response->body == "fake stdout body" && response->body.find("STDERR") == std::string::npos,
           "nonstream curl parses stdout independently and never contaminates the body with stderr");
    expect(settled_record(record, ava::process::TerminationReasonV1::NaturalExit) && record->stderr_bytes == std::string_view("FAKE_CURL_STDERR_CANARY").size(),
           "separate stderr is fully drained and accounted without exposing content");
  }
  {
    CurlFixture fixture;
    auto response = fixture.transport.send(request_for("/buffered-hup"));
    fixture.capture_snapshot();
    expect(response && response->body == std::string(32U * 1024U, 'h') && settled_record(fixture.only_record(), ava::process::TerminationReasonV1::NaturalExit),
           "buffered stdout is drained through HUP/EOF before successful settlement");
  }
  {
    CurlFixture fixture;
    std::vector<std::string> chunks;
    auto response = fixture.transport.send_streaming(request_for("/stream"), [&](std::string_view chunk) -> ava::core::VoidResult {
      chunks.emplace_back(chunk);
      return {};
    });
    fixture.capture_snapshot();
    std::string delivered;
    for (auto const& chunk : chunks)
      delivered += chunk;
    expect(response && response->status_code == 206 && response->body == "chunk-one|chunk-two" && delivered == response->body &&
               delivered.find("AVA_HTTP_STATUS") == std::string::npos,
           "streaming curl preserves callback order while withholding a split status marker");
    expect(settled_record(fixture.only_record(), ava::process::TerminationReasonV1::NaturalExit),
           "streaming completion waits for both output streams and exact process settlement");
  }
}

void test_stop_reasons_and_limits()
{
  {
    CurlFixture fixture;
    auto response = fixture.transport.send(request_for("/cancel"), [] { return true; });
    fixture.capture_snapshot();
    expect(!response && response.error().message() == "transport request canceled" &&
               settled_record(fixture.only_record(), ava::process::TerminationReasonV1::Canceled),
           "pre-deadline cancellation requests supervised Canceled cleanup and preserves the cancellation message");
  }
  {
    CurlFixture fixture;
    auto response = fixture.transport.send(request_for("/output-limit", 5000));
    fixture.capture_snapshot();
    auto const* record = fixture.only_record();
    expect(!response && response.error().message() == "curl response exceeded byte limit" &&
               settled_record(record, ava::process::TerminationReasonV1::OutputLimit) && record->stdout_truncated && record->stdout_bytes > 8U * 1024U * 1024U,
           "curl stdout hard cap requests OutputLimit while continuing raw output accounting");
  }
  {
    CurlFixture fixture;
    auto const started = std::chrono::steady_clock::now();
    auto response = fixture.transport.send(request_for("/progress-timeout", 120));
    auto const elapsed = std::chrono::steady_clock::now() - started;
    fixture.capture_snapshot();
    expect(!response && response.error().message() == "curl transport deadline expired" && elapsed < 1500ms &&
               settled_record(fixture.only_record(), ava::process::TerminationReasonV1::DeadlineExpired),
           "periodic curl progress never resets the one absolute request deadline");
  }
  {
    CurlFixture fixture;
    auto response = fixture.transport.send_streaming(request_for("/stream"), [](std::string_view) -> ava::core::VoidResult {
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Tool, "original sink failure"));
    });
    fixture.capture_snapshot();
    expect(!response && response.error().category() == ava::core::ErrorCategory::Tool && response.error().message() == "original sink failure" &&
               settled_record(fixture.only_record(), ava::process::TerminationReasonV1::ProtocolFailure),
           "stream sink failure returns the original error after supervised ProtocolFailure cleanup");
  }
  {
    CurlFixture fixture;
    auto request = request_for("/config-failure");
    request.headers.emplace("X-Large", std::string(256U * 1024U, 'c'));
    auto response = fixture.transport.send(request);
    fixture.capture_snapshot();
    expect(!response && response.error().message() == "failed to write curl configuration" &&
               settled_record(fixture.only_record(), ava::process::TerminationReasonV1::ProtocolFailure),
           "incremental config pipe failure uses supervised ProtocolFailure cleanup without retry");
  }
  {
    CurlFixture fixture;
    auto response = fixture.transport.send(request_for("/protocol"));
    fixture.capture_snapshot();
    expect(!response && response.error().message() == "curl response did not include an HTTP status" &&
               settled_record(fixture.only_record(), ava::process::TerminationReasonV1::ProtocolFailure),
           "missing curl status marker is a supervised protocol failure");
  }
}

void test_group_cleanup_and_stderr_truncation()
{
  {
    CurlFixture fixture;
    auto response = fixture.transport.send(request_for("/descendant"));
    fixture.capture_snapshot();
    expect(response && response->body == "descendant body" && settled_record(fixture.only_record(), ava::process::TerminationReasonV1::NaturalExit) &&
               no_waitable_immediate_child(),
           "a same-group descendant surviving its curl leader is killed before endpoint EOF and complete settlement");
  }
  {
    CurlFixture fixture;
    auto const started = std::chrono::steady_clock::now();
    auto response = fixture.transport.send(request_for("/term-refusal"), [started] { return std::chrono::steady_clock::now() - started > 100ms; });
    fixture.capture_snapshot();
    expect(!response && settled_record(fixture.only_record(), ava::process::TerminationReasonV1::Canceled) && no_waitable_immediate_child(),
           "curl cancellation escalates a TERM-refusing group and leaves no waitable child");
  }
  {
    CurlFixture fixture;
    auto response = fixture.transport.send(request_for("/stderr-limit"));
    fixture.capture_snapshot();
    auto const* record = fixture.only_record();
    expect(response && response->body == "stderr bounded" && settled_record(record, ava::process::TerminationReasonV1::NaturalExit) &&
               record->stderr_bytes == 72U * 1024U && record->stderr_truncated,
           "stream-independent stderr retention truncates at 64 KiB while draining and accounting all bytes");
  }
}

void test_natural_failures_exec_failure_and_redaction()
{
  constexpr std::string_view url_canary = "CURL_URL_SECRET_CANARY";
  {
    CurlFixture fixture;
    auto response = fixture.transport.send(request_for("/nonzero"));
    fixture.capture_snapshot();
    auto const formatted = response ? std::string{} : response.error().format();
    auto const* record = fixture.only_record();
    expect(!response && response.error().message() == "curl transport failed" && formatted.find("NONZERO_SECRET_CANARY") == std::string::npos &&
               formatted.find("exit_code: 7") != std::string::npos && settled_record(record, ava::process::TerminationReasonV1::NaturalExit) &&
               record->has_exit_code && record->exit_code == 7,
           "nonzero curl exits map to content-free generic metadata with no legacy retry");
  }
  {
    CurlFixture fixture;
    auto response = fixture.transport.send(request_for("/signal"));
    fixture.capture_snapshot();
    auto const* record = fixture.only_record();
    expect(!response && response.error().message() == "curl transport failed" && settled_record(record, ava::process::TerminationReasonV1::NaturalExit) &&
               record->has_signal_number && record->settlement_count == 1,
           "naturally signaled curl exits remain one generic exactly-once record");
  }
  {
    CurlFixture fixture;
    ava::process::testing::SupervisorTestAccess::fail_next_common_child_working_directory(*fixture.supervisor);
    auto response = fixture.transport.send(request_for("/success"));
    fixture.capture_snapshot();
    auto const* record = fixture.only_record();
    expect(!response && response.error().message() == "curl transport failed" && record != nullptr &&
               record->reason == ava::process::TerminationReasonV1::LaunchFailed && record->state == ava::process::ProcessStateV1::Finished &&
               record->cleanup == ava::process::CleanupStateV1::Complete && record->settlement_count == 1 && fixture.snapshot.live_records == 0,
           "curl post-fork launch failure is generic, completely cleaned, exactly once settled, and never retried");
  }
  {
    char path_template[] = "/tmp/ava-fake-curl-exec-failure-XXXXXX";
    int const descriptor = ::mkstemp(path_template);
    expect(descriptor >= 0, "exec-failure fixture is created");
    if (descriptor >= 0)
    {
      auto const written = ::write(descriptor, "not an executable format\n", 25);
      expect(written == 25, "exec-failure fixture content is written");
      static_cast<void>(::close(descriptor));
      static_cast<void>(::chmod(path_template, S_IRUSR | S_IWUSR | S_IXUSR));
      CurlFixture fixture;
      auto installed = ava::http::testing::CurlTransportTestAccess::set_executable(fixture.transport, path_template);
      auto response =
          installed ? fixture.transport.send(request_for("/success")) : ava::core::Result<ava::http::HttpResponse>(std::unexpected(installed.error()));
      fixture.capture_snapshot();
      auto const* record = fixture.only_record();
      expect(!response && response.error().message() == "curl transport failed" && record != nullptr &&
                 record->reason == ava::process::TerminationReasonV1::ExecFailed && record->state == ava::process::ProcessStateV1::Finished &&
                 record->cleanup == ava::process::CleanupStateV1::Complete && record->settlement_count == 1 && fixture.snapshot.live_records == 0,
             "curl exec failure is generic, supervised, completely cleaned, and never retried");
      static_cast<void>(::unlink(path_template));
    }
  }
  {
    CurlFixture fixture;
    auto request = request_for("/nonzero");
    request.url += std::string(url_canary);
    request.headers.emplace("X-Secret", "HEADER_SECRET_CANARY");
    request.body = "BODY_SECRET_CANARY";
    auto response = fixture.transport.send(request);
    fixture.capture_snapshot();
    auto const formatted = response ? std::string{} : response.error().format();
    expect(!response && formatted.find(url_canary) == std::string::npos && formatted.find("HEADER_SECRET_CANARY") == std::string::npos &&
               formatted.find("BODY_SECRET_CANARY") == std::string::npos && formatted.find("NONZERO_SECRET_CANARY") == std::string::npos,
           "curl errors omit URL, config, header, body, stdout, and stderr canaries");
  }
}

}  // namespace

void run_curl_transport_process_tests()
{
  expect(std::string_view(AVA_FAKE_CURL_CHILD_PATH).starts_with('/'), "repository-owned fake curl child has an absolute test path");
  test_exact_argv_config_body_and_success();
  test_exact_environment_capture();
  test_stream_separation_hup_and_callbacks();
  test_stop_reasons_and_limits();
  test_group_cleanup_and_stderr_truncation();
  test_natural_failures_exec_failure_and_redaction();
}
