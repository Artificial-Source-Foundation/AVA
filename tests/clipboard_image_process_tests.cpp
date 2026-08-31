#include "sys.h"
#include "tests/support/test_harness.h"
#include "ava/process/environment.h"
#include "ava/process/scope.h"
#include "ava/process/supervisor.h"
#include "ava/app/clipboard_image.h"
#include "ava/app/clipboard_image_test_support.h"
#include "ava/session/attachments.h"
#include "ava/session/session_store.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef AVA_FAKE_CLIPBOARD_CHILD_PATH
#define AVA_FAKE_CLIPBOARD_CHILD_PATH ""
#endif

namespace {

using namespace std::chrono_literals;

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

ava::process::ProcessScopeV1 require_session_scope(std::shared_ptr<ava::process::Supervisor> const& supervisor)
{
  auto application = ava::process::ProcessScopeV1::application(supervisor);
  if (!application)
    throw std::runtime_error(application.error().format());
  auto session = application->session();
  if (!session)
    throw std::runtime_error(session.error().format());
  return *session;
}

std::string tiny_png()
{
  std::string bytes;
  bytes.push_back(static_cast<char>(0x89));
  bytes += "PNG\r\n";
  bytes.push_back(static_cast<char>(0x1A));
  bytes += "\nava-clipboard-image";
  return bytes;
}

class ClipboardFixture final
{
 public:
  explicit ClipboardFixture(std::string_view label)
      : root(create_empty_root("clipboard-process-" + std::string(label))),
        workspace(root / "workspace"),
        store({.root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "clipboard-session"}),
        supervisor(std::make_shared<ava::process::Supervisor>()),
        scope(require_session_scope(supervisor)),
        log(root / "invocations.log")
  {
    std::filesystem::create_directories(workspace);
  }

  [[nodiscard]] ava::core::Result<std::optional<ava::session::ImageAttachmentRef>> import(std::string scenario)
  {
    return ava::app::testing::ClipboardImageTestAccess::import_with_helper(store, scope, AVA_FAKE_CLIPBOARD_CHILD_PATH, std::move(scenario), log);
  }

  [[nodiscard]] ava::process::ProcessSnapshotV1 snapshot() const { return supervisor->snapshot(); }

  std::filesystem::path root;
  std::filesystem::path workspace;
  ava::session::SessionStore store;
  std::shared_ptr<ava::process::Supervisor> supervisor;
  ava::process::ProcessScopeV1 scope;
  std::filesystem::path log;
};

struct LoggedInvocation
{
  std::vector<std::string> arguments;
  std::map<std::string, std::string> environment;
};

std::vector<LoggedInvocation> read_invocation_log(std::filesystem::path const& path)
{
  std::ifstream input(path);
  std::vector<LoggedInvocation> result;
  std::optional<LoggedInvocation> current;
  std::string line;
  while (std::getline(input, line))
  {
    if (line == "BEGIN")
    {
      current.emplace();
      continue;
    }
    if (line == "END")
    {
      if (current)
        result.push_back(std::move(*current));
      current.reset();
      continue;
    }
    if (!current)
      continue;
    if (line.starts_with("ARG:"))
    {
      current->arguments.push_back(line.substr(4));
      continue;
    }
    if (line.starts_with("ENV:"))
    {
      auto const separator = line.find('=', 4);
      if (separator != std::string::npos)
        current->environment.emplace(line.substr(4, separator - 4), line.substr(separator + 1));
    }
  }
  return result;
}

bool no_waitable_immediate_child()
{
  errno = 0;
  int status = 0;
  auto const child = ::waitpid(-1, &status, WNOHANG);
  return child == -1 && errno == ECHILD;
}

bool all_records_settled(ava::process::ProcessSnapshotV1 const& snapshot, std::size_t count)
{
  if (snapshot.records.size() != count || snapshot.live_records != 0)
    return false;
  std::set<std::uint64_t> owners;
  for (auto const& record : snapshot.records)
  {
    if (record.role != ava::process::ProcessRoleV1::ClipboardHelper || record.state != ava::process::ProcessStateV1::Finished ||
        record.cleanup != ava::process::CleanupStateV1::Complete || record.settlement_count != 1)
    {
      return false;
    }
    owners.insert(record.owner_alias);
  }
  return owners.size() == count;
}

bool successful_import(ava::core::Result<std::optional<ava::session::ImageAttachmentRef>> const& result)
{
  return result && result->has_value() && (**result).mime_type == "image/png" && (**result).byte_size > 0;
}

void clear_clipboard_parent_selection()
{
  static_cast<void>(::unsetenv("AVA_CLIPBOARD_IMAGE_FILE"));
  static_cast<void>(::unsetenv("TERMUX_VERSION"));
}

void test_settled_status_requires_complete_cleanup()
{
  using Disposition = ava::app::testing::ClipboardHelperStatusDisposition;

  ava::process::ExitStatusV1 success{
      .reason = ava::process::TerminationReasonV1::NaturalExit,
      .kind = ava::process::ExitKindV1::Exited,
      .cleanup = ava::process::CleanupStateV1::Complete,
      .exit_code = 0,
      .has_exit_code = true,
  };
  auto accepted = ava::app::testing::ClipboardImageTestAccess::classify_helper_status(success, false);

  auto nonzero = success;
  nonzero.exit_code = 7;
  auto unavailable = ava::app::testing::ClipboardImageTestAccess::classify_helper_status(nonzero, false);

  ava::process::ExitStatusV1 output_limit{
      .reason = ava::process::TerminationReasonV1::OutputLimit,
      .kind = ava::process::ExitKindV1::Signaled,
      .cleanup = ava::process::CleanupStateV1::Complete,
      .signal_number = SIGTERM,
      .has_signal_number = true,
  };
  auto limited = ava::app::testing::ClipboardImageTestAccess::classify_helper_status(output_limit, true);
  expect(accepted && *accepted == Disposition::Accepted && unavailable && *unavailable == Disposition::Unavailable && limited &&
             *limited == Disposition::OutputLimit,
         "complete helper cleanup preserves success, ordinary nonzero fallback, and the specific output-limit disposition");

  success.cleanup = ava::process::CleanupStateV1::Incomplete;
  nonzero.cleanup = ava::process::CleanupStateV1::Incomplete;
  output_limit.cleanup = ava::process::CleanupStateV1::Incomplete;
  auto incomplete_success = ava::app::testing::ClipboardImageTestAccess::classify_helper_status(success, false);
  auto incomplete_nonzero = ava::app::testing::ClipboardImageTestAccess::classify_helper_status(nonzero, false);
  auto incomplete_limit = ava::app::testing::ClipboardImageTestAccess::classify_helper_status(output_limit, true);
  auto is_lifecycle_error = [](auto const& result) {
    return !result && result.error().category() == ava::core::ErrorCategory::Io &&
           result.error().message() == "clipboard helper process cleanup did not complete";
  };
  expect(is_lifecycle_error(incomplete_success) && is_lifecycle_error(incomplete_nonzero) && is_lifecycle_error(incomplete_limit),
         "incomplete cleanup rejects success bytes, nonzero fallback, and output-limit classification with one content-free lifecycle error");
}

void test_pre_reservation_deadline_is_unavailable_without_record()
{
  ClipboardFixture fixture("pre-reservation-deadline");
  auto const started = std::chrono::steady_clock::now();
  auto result = ava::app::testing::ClipboardImageTestAccess::capture_list_after_preparation_delay(fixture.scope, AVA_FAKE_CLIPBOARD_CHILD_PATH,
                                                                                                  "pre-reservation-deadline", fixture.log, 1100ms);
  auto const elapsed = std::chrono::steady_clock::now() - started;
  auto const snapshot = fixture.snapshot();
  expect(result && !*result && elapsed >= 1050ms && elapsed < 2500ms && snapshot.records.empty() && snapshot.live_records == 0 && !snapshot.monitor_started &&
             !std::filesystem::exists(fixture.log) && no_waitable_immediate_child(),
         "parent preparation exhausting the helper deadline maps to unavailable before reservation, monitor startup, or child exec");
}

void test_file_override_and_no_helper_branches_need_no_scope()
{
  EnvironmentRestore restore({"AVA_CLIPBOARD_IMAGE_FILE", "TERMUX_VERSION", "WAYLAND_DISPLAY"});
  clear_clipboard_parent_selection();
  ClipboardFixture fixture("no-scope");

  auto const image_path = fixture.workspace / "override.png";
  {
    std::ofstream output(image_path, std::ios::binary);
    output << tiny_png();
  }
  static_cast<void>(::setenv("AVA_CLIPBOARD_IMAGE_FILE", image_path.c_str(), 1));
  auto overridden = ava::app::import_clipboard_image_attachment(fixture.store, std::nullopt);
  auto snapshot = fixture.snapshot();
  expect(successful_import(overridden) && snapshot.records.empty() && snapshot.live_records == 0 && !snapshot.monitor_started,
         "clipboard file override imports with missing process scope and starts no reservation or monitor");

  static_cast<void>(::unsetenv("AVA_CLIPBOARD_IMAGE_FILE"));
  static_cast<void>(::setenv("TERMUX_VERSION", "termux-parent-only", 1));
  auto termux =
      ava::app::testing::ClipboardImageTestAccess::import_with_helper(fixture.store, std::nullopt, AVA_FAKE_CLIPBOARD_CHILD_PATH, "success", fixture.log);
  snapshot = fixture.snapshot();
  expect(termux && !*termux && snapshot.records.empty() && snapshot.live_records == 0 && !snapshot.monitor_started && !std::filesystem::exists(fixture.log),
         "TERMUX clipboard short circuit needs no process scope and starts no helper authority");

  static_cast<void>(::unsetenv("TERMUX_VERSION"));
  auto missing =
      ava::app::testing::ClipboardImageTestAccess::import_with_helper(fixture.store, std::nullopt, AVA_FAKE_CLIPBOARD_CHILD_PATH, "success", fixture.log);
  snapshot = fixture.snapshot();
  expect(!missing && missing.error().category() == ava::core::ErrorCategory::Configuration &&
             missing.error().message() == "clipboard helper process scope is unavailable" && snapshot.records.empty() && snapshot.live_records == 0 &&
             !snapshot.monitor_started && !std::filesystem::exists(fixture.log),
         "real clipboard helper selection rejects missing session scope before reservation");
}

void test_exact_wayland_and_xclip_argv_order()
{
  EnvironmentRestore restore({"AVA_CLIPBOARD_IMAGE_FILE", "TERMUX_VERSION", "WAYLAND_DISPLAY"});
  clear_clipboard_parent_selection();

  static_cast<void>(::setenv("WAYLAND_DISPLAY", "wayland-test", 1));
  ClipboardFixture wayland("wayland-order");
  auto wayland_result = wayland.import("wayland-order");
  auto const wayland_log = read_invocation_log(wayland.log);
  auto const wayland_snapshot = wayland.snapshot();
  expect(successful_import(wayland_result) && wayland_log.size() == 2 && wayland_log[0].arguments == std::vector<std::string>{"wl-paste", "--list-types"} &&
             wayland_log[1].arguments == std::vector<std::string>{"wl-paste", "--type", "image/png", "--no-newline"},
         "Wayland clipboard list/data helpers preserve exact argv and PNG-first MIME priority");
  expect(all_records_settled(wayland_snapshot, 2) && no_waitable_immediate_child(),
         "Wayland list/data helpers each use one distinct operation and settle one complete ClipboardHelper record");

  static_cast<void>(::unsetenv("WAYLAND_DISPLAY"));
  ClipboardFixture xclip("xclip-order");
  auto xclip_result = xclip.import("xclip-order");
  auto const xclip_log = read_invocation_log(xclip.log);
  auto const xclip_snapshot = xclip.snapshot();
  std::vector<std::vector<std::string>> const expected{
      {"xclip", "-selection", "clipboard", "-t", "TARGETS", "-o"},
      {"xclip", "-selection", "clipboard", "-t", "image/jpeg", "-o"},
      {"xclip", "-selection", "clipboard", "-t", "image/png", "-o"},
      {"xclip", "-selection", "clipboard", "-t", "image/webp", "-o"},
  };
  std::vector<std::vector<std::string>> observed;
  for (auto const& invocation : xclip_log)
    observed.push_back(invocation.arguments);
  expect(successful_import(xclip_result) && observed == expected,
         "xclip preserves TARGETS/data fallback order, JPEG priority, MIME deduplication, nonzero fallback, and empty fallback");
  expect(all_records_settled(xclip_snapshot, 4) && xclip_snapshot.records[1].has_exit_code && xclip_snapshot.records[1].exit_code == 7 &&
             xclip_snapshot.records[2].has_exit_code && xclip_snapshot.records[2].exit_code == 0 && no_waitable_immediate_child(),
         "xclip nonzero and empty data attempts remain natural exactly-once records before successful fallback");
}

bool clipboard_environment_name_allowed(std::string_view name)
{
  static std::set<std::string_view> const fixed{
      "PATH",
      "HOME",
      "USER",
      "LOGNAME",
      "TMPDIR",
      "TMP",
      "TEMP",
      "LANG",
      "LANGUAGE",
      "LC_ALL",
      "XDG_RUNTIME_DIR",
      "DISPLAY",
      "WAYLAND_DISPLAY",
      "XAUTHORITY",
      "DBUS_SESSION_BUS_ADDRESS",
  };
  return fixed.contains(name) || (name.starts_with("LC_") && name != "LC_ALL");
}

void test_exact_clipboard_environment_capture()
{
  std::vector<std::string> names{
      "AVA_CLIPBOARD_IMAGE_FILE",
      "TERMUX_VERSION",
      "WAYLAND_DISPLAY",
      "PATH",
      "HOME",
      "USER",
      "LOGNAME",
      "TMPDIR",
      "TMP",
      "TEMP",
      "LANG",
      "LANGUAGE",
      "LC_ALL",
      "LC_CLIPBOARD_TEST",
      "XDG_RUNTIME_DIR",
      "DISPLAY",
      "XAUTHORITY",
      "DBUS_SESSION_BUS_ADDRESS",
      "HTTPS_PROXY",
      "CURL_CA_BUNDLE",
      "OPENAI_API_KEY",
      "AWS_SECRET_ACCESS_KEY",
      "LD_PRELOAD",
      "GIT_ASKPASS",
      "SSH_AUTH_SOCK",
      "AVA_CLIPBOARD_SECRET",
      "AVA_CLIPBOARD_TEST_SCENARIO",
  };
  EnvironmentRestore restore(std::move(names));
  clear_clipboard_parent_selection();

  static_cast<void>(::setenv("PATH", "/captured/not/forwarded", 1));
  static_cast<void>(::setenv("HOME", "/captured/home", 1));
  static_cast<void>(::setenv("USER", "captured-user", 1));
  static_cast<void>(::setenv("LOGNAME", "captured-logname", 1));
  static_cast<void>(::setenv("TMPDIR", "/tmp", 1));
  static_cast<void>(::setenv("TMP", "/captured/tmp", 1));
  static_cast<void>(::setenv("TEMP", "/captured/temp", 1));
  static_cast<void>(::setenv("LANG", "captured-lang", 1));
  static_cast<void>(::setenv("LANGUAGE", "captured-language", 1));
  static_cast<void>(::setenv("LC_ALL", "captured-lc-all", 1));
  static_cast<void>(::setenv("LC_CLIPBOARD_TEST", "captured-locale", 1));
  static_cast<void>(::setenv("XDG_RUNTIME_DIR", "/captured/runtime", 1));
  static_cast<void>(::setenv("DISPLAY", ":captured", 1));
  static_cast<void>(::setenv("WAYLAND_DISPLAY", "captured-wayland", 1));
  static_cast<void>(::setenv("XAUTHORITY", "/captured/xauthority", 1));
  static_cast<void>(::setenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/captured/dbus", 1));
  static_cast<void>(::setenv("HTTPS_PROXY", "https://proxy-canary.invalid", 1));
  static_cast<void>(::setenv("CURL_CA_BUNDLE", "/ca-canary.pem", 1));
  static_cast<void>(::setenv("OPENAI_API_KEY", "PROVIDER_CANARY", 1));
  static_cast<void>(::setenv("AWS_SECRET_ACCESS_KEY", "CLOUD_CANARY", 1));
  static_cast<void>(::setenv("LD_PRELOAD", "/loader-canary.so", 1));
  static_cast<void>(::setenv("GIT_ASKPASS", "/askpass-canary", 1));
  static_cast<void>(::setenv("SSH_AUTH_SOCK", "/agent-canary", 1));
  static_cast<void>(::setenv("AVA_CLIPBOARD_SECRET", "ARBITRARY_AVA_CANARY", 1));
  static_cast<void>(::setenv("AVA_CLIPBOARD_TEST_SCENARIO", "AMBIENT_TEST_CANARY", 1));
  static_cast<void>(::setenv("TERMUX_VERSION", "PARENT_SELECTION_CANARY", 1));
  static_cast<void>(::setenv("AVA_CLIPBOARD_IMAGE_FILE", "/parent/selection/canary", 1));

  ClipboardFixture fixture("environment");

  static_cast<void>(::setenv("HOME", "/mutated/home", 1));
  static_cast<void>(::setenv("DISPLAY", ":mutated", 1));
  static_cast<void>(::setenv("WAYLAND_DISPLAY", "mutated-wayland", 1));
  static_cast<void>(::setenv("DBUS_SESSION_BUS_ADDRESS", "unix:path=/mutated/dbus", 1));
  static_cast<void>(::unsetenv("TERMUX_VERSION"));
  static_cast<void>(::unsetenv("AVA_CLIPBOARD_IMAGE_FILE"));

  auto result = fixture.import("environment");
  auto const invocations = read_invocation_log(fixture.log);
  bool exact = successful_import(result) && invocations.size() == 2;
  for (auto const& invocation : invocations)
  {
    auto const& environment = invocation.environment;
    exact = exact && std::ranges::all_of(environment, [](auto const& variable) { return clipboard_environment_name_allowed(variable.first); });
    auto value_is = [&](std::string const& name, std::string const& value) {
      auto const found = environment.find(name);
      return found != environment.end() && found->second == value;
    };
    exact = exact && value_is("PATH", std::string(ava::process::kTrustedEnvironmentPathV1)) && value_is("HOME", "/captured/home") &&
            value_is("USER", "captured-user") && value_is("LOGNAME", "captured-logname") && value_is("TMPDIR", "/tmp") && value_is("TMP", "/captured/tmp") &&
            value_is("TEMP", "/captured/temp") && value_is("LANG", "captured-lang") && value_is("LANGUAGE", "captured-language") &&
            value_is("LC_ALL", "captured-lc-all") && value_is("LC_CLIPBOARD_TEST", "captured-locale") && value_is("XDG_RUNTIME_DIR", "/captured/runtime") &&
            value_is("DISPLAY", ":captured") && value_is("WAYLAND_DISPLAY", "captured-wayland") && value_is("XAUTHORITY", "/captured/xauthority") &&
            value_is("DBUS_SESSION_BUS_ADDRESS", "unix:path=/captured/dbus");
    for (auto const forbidden : {"HTTPS_PROXY", "CURL_CA_BUNDLE", "OPENAI_API_KEY", "AWS_SECRET_ACCESS_KEY", "LD_PRELOAD", "GIT_ASKPASS", "SSH_AUTH_SOCK",
                                 "AVA_CLIPBOARD_SECRET", "AVA_CLIPBOARD_TEST_SCENARIO", "TERMUX_VERSION", "AVA_CLIPBOARD_IMAGE_FILE"})
    {
      exact = exact && !environment.contains(forbidden);
    }
    for (auto const mutated : {"/mutated/home", ":mutated", "mutated-wayland", "unix:path=/mutated/dbus"})
    {
      exact = exact && std::ranges::none_of(environment, [&](auto const& variable) { return variable.second == mutated; });
    }
  }
  expect(exact,
         "ava-clipboard-desktop-v1 forwards exactly captured desktop values and excludes proxy, CA, provider, cloud, loader, askpass, agent, AVA, and "
         "parent-only selection/test values");
  expect(all_records_settled(fixture.snapshot(), 2), "clipboard environment inspection helpers remain completely and exactly-once supervised");
}

void test_buffered_hup_and_absolute_deadlines()
{
  EnvironmentRestore restore({"AVA_CLIPBOARD_IMAGE_FILE", "TERMUX_VERSION", "WAYLAND_DISPLAY"});
  clear_clipboard_parent_selection();

  static_cast<void>(::setenv("WAYLAND_DISPLAY", "wayland-buffered", 1));
  ClipboardFixture buffered("buffered-hup");
  auto buffered_result = buffered.import("buffered-hup");
  auto const buffered_snapshot = buffered.snapshot();
  expect(successful_import(buffered_result) && all_records_settled(buffered_snapshot, 2) && buffered_snapshot.records[1].stdout_bytes == 32U * 1024U &&
             !buffered_snapshot.records[1].stdout_truncated,
         "clipboard drains buffered HUP bytes through EOF before exact natural settlement");

  static_cast<void>(::unsetenv("WAYLAND_DISPLAY"));
  ClipboardFixture list_deadline("list-deadline");
  auto const list_started = std::chrono::steady_clock::now();
  auto list_result = list_deadline.import("list-deadline");
  auto const list_elapsed = std::chrono::steady_clock::now() - list_started;
  auto const list_snapshot = list_deadline.snapshot();
  expect(successful_import(list_result) && list_elapsed >= 900ms && list_elapsed < 2500ms && all_records_settled(list_snapshot, 2) &&
             list_snapshot.records[0].reason == ava::process::TerminationReasonV1::DeadlineExpired && list_snapshot.records[0].stdout_bytes > 0 &&
             !list_snapshot.records[0].stdout_truncated,
         "clipboard list progress never resets its one-second absolute DeadlineExpired reservation");

  ClipboardFixture image_deadline("image-deadline");
  auto const image_started = std::chrono::steady_clock::now();
  auto image_result = image_deadline.import("term-refusal");
  auto const image_elapsed = std::chrono::steady_clock::now() - image_started;
  auto const image_snapshot = image_deadline.snapshot();
  expect(successful_import(image_result) && image_elapsed >= 2900ms && image_elapsed < 5s && all_records_settled(image_snapshot, 3) &&
             image_snapshot.records[1].reason == ava::process::TerminationReasonV1::DeadlineExpired && image_snapshot.records[1].stdout_bytes > 0 &&
             !image_snapshot.records[1].stdout_truncated && no_waitable_immediate_child(),
         "clipboard image progress uses one three-second deadline and completely escalates a TERM-refusing helper before fallback");
}

void test_output_limits_and_group_cleanup()
{
  EnvironmentRestore restore({"AVA_CLIPBOARD_IMAGE_FILE", "TERMUX_VERSION", "WAYLAND_DISPLAY"});
  clear_clipboard_parent_selection();
  static_cast<void>(::unsetenv("WAYLAND_DISPLAY"));

  ClipboardFixture list_limit("list-limit");
  auto list_result = list_limit.import("list-limit");
  auto const list_snapshot = list_limit.snapshot();
  expect(successful_import(list_result) && all_records_settled(list_snapshot, 2) &&
             list_snapshot.records[0].reason == ava::process::TerminationReasonV1::OutputLimit && list_snapshot.records[0].stdout_bytes > 64U * 1024U &&
             list_snapshot.records[0].stdout_truncated,
         "oversized clipboard target list requests OutputLimit, accounts the drained bytes, and falls back without retaining truncation");

  ClipboardFixture image_limit("image-limit");
  auto image_result = image_limit.import("image-limit");
  auto const image_snapshot = image_limit.snapshot();
  auto const formatted = image_result ? std::string{} : image_result.error().format();
  expect(!image_result && image_result.error().message() == "clipboard image is too large" && formatted.find("ARBITRARY_AVA_CANARY") == std::string::npos &&
             all_records_settled(image_snapshot, 2) && image_snapshot.records[1].reason == ava::process::TerminationReasonV1::OutputLimit &&
             image_snapshot.records[1].stdout_bytes == ava::session::kMaxImageAttachmentBytes + 1 && image_snapshot.records[1].stdout_truncated,
         "oversized clipboard image requests OutputLimit with full observed accounting and preserves the actionable final size error");

  static_cast<void>(::setenv("WAYLAND_DISPLAY", "wayland-descendant", 1));
  ClipboardFixture descendant("descendant");
  auto descendant_result = descendant.import("descendant");
  auto const descendant_snapshot = descendant.snapshot();
  expect(successful_import(descendant_result) && all_records_settled(descendant_snapshot, 2) &&
             descendant_snapshot.records[1].reason == ava::process::TerminationReasonV1::NaturalExit && no_waitable_immediate_child(),
         "clipboard natural leader cleanup kills a same-group TERM-refusing descendant before EOF and complete settlement");
}

void test_incomplete_cleanup_stops_helper_fallback()
{
  EnvironmentRestore restore({"AVA_CLIPBOARD_IMAGE_FILE", "TERMUX_VERSION", "WAYLAND_DISPLAY"});
  clear_clipboard_parent_selection();
  static_cast<void>(::unsetenv("WAYLAND_DISPLAY"));

  ClipboardFixture fixture("cleanup-incomplete");
  auto result = fixture.import("cleanup-incomplete");
  auto const invocations = read_invocation_log(fixture.log);
  auto const snapshot = fixture.snapshot();
  auto const formatted = result ? std::string{} : result.error().format();
  expect(!result && result.error().category() == ava::core::ErrorCategory::Io &&
             result.error().message() == "clipboard helper process cleanup did not complete" && invocations.size() == 1 &&
             invocations[0].arguments == std::vector<std::string>{"xclip", "-selection", "clipboard", "-t", "TARGETS", "-o"} &&
             all_records_settled(snapshot, 1) && formatted.find("cleanup-incomplete") == std::string::npos &&
             formatted.find("image/png") == std::string::npos && no_waitable_immediate_child(),
         "an injected incomplete settlement returns a content-free lifecycle error without importing bytes or trying another helper");
}

void test_exec_and_read_failures_have_no_legacy_retry()
{
  EnvironmentRestore restore({"AVA_CLIPBOARD_IMAGE_FILE", "TERMUX_VERSION", "WAYLAND_DISPLAY"});
  clear_clipboard_parent_selection();
  static_cast<void>(::unsetenv("WAYLAND_DISPLAY"));

  ClipboardFixture exec_failure("exec-failure");
  auto const malformed = exec_failure.root / "malformed-helper";
  {
    std::ofstream output(malformed);
    output << "not an executable image\n";
  }
  static_cast<void>(::chmod(malformed.c_str(), S_IRUSR | S_IWUSR | S_IXUSR));
  auto exec_result = ava::app::testing::ClipboardImageTestAccess::import_with_helper(exec_failure.store, exec_failure.scope, malformed, "EXEC_FAILURE_CANARY",
                                                                                     exec_failure.log);
  auto const exec_snapshot = exec_failure.snapshot();
  bool exec_records =
      exec_result && !*exec_result && exec_snapshot.records.size() == 6 && exec_snapshot.live_records == 0 && !std::filesystem::exists(exec_failure.log);
  for (auto const& record : exec_snapshot.records)
  {
    exec_records = exec_records && record.role == ava::process::ProcessRoleV1::ClipboardHelper && record.state == ava::process::ProcessStateV1::Finished &&
                   record.reason == ava::process::TerminationReasonV1::ExecFailed && record.cleanup == ava::process::CleanupStateV1::Complete &&
                   record.settlement_count == 1;
  }
  expect(exec_records && no_waitable_immediate_child(),
         "clipboard exec failure continues fixed helper selection through Supervisor records without any legacy process fallback");

  static_cast<void>(::setenv("WAYLAND_DISPLAY", "wayland-read-failure", 1));
  ClipboardFixture read_failure("read-failure");
  auto read_result = read_failure.import("read-failure");
  auto const read_snapshot = read_failure.snapshot();
  auto const read_error = read_result ? std::string{} : read_result.error().format();
  expect(!read_result && read_result.error().category() == ava::core::ErrorCategory::Io &&
             read_result.error().message() == "failed to read clipboard helper output" && read_error.find("read-failure") == std::string::npos &&
             all_records_settled(read_snapshot, 1) && read_snapshot.records[0].reason == ava::process::TerminationReasonV1::ProtocolFailure &&
             no_waitable_immediate_child(),
         "clipboard read failure requests ProtocolFailure, settles once, and omits scenario/output canaries from errors");
}

}  // namespace

void run_clipboard_image_process_tests()
{
  expect(std::string_view(AVA_FAKE_CLIPBOARD_CHILD_PATH).starts_with('/'), "repository-owned fake clipboard child has an absolute test path");
  test_settled_status_requires_complete_cleanup();
  test_pre_reservation_deadline_is_unavailable_without_record();
  test_file_override_and_no_helper_branches_need_no_scope();
  test_exact_wayland_and_xclip_argv_order();
  test_exact_clipboard_environment_capture();
  test_buffered_hup_and_absolute_deadlines();
  test_output_limits_and_group_cleanup();
  test_incomplete_cleanup_stops_helper_fallback();
  test_exec_and_read_failures_have_no_legacy_retry();
}
