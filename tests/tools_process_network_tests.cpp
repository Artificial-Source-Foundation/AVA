#include "sys.h"
#include "tests/support/test_harness.h"
#include "ava/agent/mode.h"
#include "ava/tools/bash_tool.h"
#include "ava/tools/spill_files.h"
#include "ava/tools/webfetch_tool.h"
#include "ava/tools/websearch_tool.h"
#include "ava/permissions/permission.h"
#include "ava/permissions/permission_rules.h"
#include "ava/provider/provider.h"
#include "ava/core/error.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>

namespace {

std::string read_text_file_for_test(std::filesystem::path const& path)
{
  std::ifstream file(path, std::ios::binary);
  std::ostringstream out;
  out << file.rdbuf();
  return out.str();
}

std::optional<pid_t> read_pid_file_for_test(std::filesystem::path const& path)
{
  std::ifstream file(path, std::ios::binary);
  long long value = 0;
  file >> value;
  if (!file || value <= 0)
    return std::nullopt;
  return static_cast<pid_t>(value);
}

bool process_group_exists(pid_t pgid)
{
  errno = 0;
  if (::kill(-pgid, 0) == 0)
    return true;
  return errno != ESRCH;
}

bool wait_for_process_group_exit(pid_t pgid)
{
  for (int index = 0; index < 100; ++index)
  {
    if (!process_group_exists(pgid))
      return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return !process_group_exists(pgid);
}

class RecordingCommandExecutor final : public ava::tools::CommandExecutor
{
 public:
  [[nodiscard]] ava::core::Result<ava::tools::CommandExecutionResult> execute(ava::tools::CommandExecutionRequest request) const override
  {
    requests.push_back(std::move(request));
    if (fail)
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "injected command execution failed"));
    return result;
  }

  mutable std::vector<ava::tools::CommandExecutionRequest> requests;
  ava::tools::CommandExecutionResult result;
  bool fail = false;
};

class StaticTransport final : public ava::provider::Transport
{
 public:
  explicit StaticTransport(ava::provider::HttpResponse response) : response_(std::move(response)) { }

  [[nodiscard]] ava::core::Result<ava::provider::HttpResponse> send(ava::provider::HttpRequest const& request) override
  {
    requests.push_back(request);
    return response_;
  }

  std::vector<ava::provider::HttpRequest> requests;

 private:
  ava::provider::HttpResponse response_;
};

class CancelAwareTransport final : public ava::provider::Transport
{
 public:
  [[nodiscard]] ava::core::Result<ava::provider::HttpResponse> send(ava::provider::HttpRequest const& request) override
  {
    requests.push_back(request);
    return ava::provider::HttpResponse{.status_code = 200, .headers = {{"content-type", "text/plain"}}, .body = "ok"};
  }

  [[nodiscard]] ava::core::Result<ava::provider::HttpResponse> send(ava::provider::HttpRequest const& request, CancelCallback cancel_requested) override
  {
    requests.push_back(request);
    saw_cancel_callback = static_cast<bool>(cancel_requested);
    if (cancel_requested && cancel_requested())
    {
      return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "transport request canceled"));
    }
    return ava::provider::HttpResponse{.status_code = 200, .headers = {{"content-type", "text/plain"}}, .body = "ok"};
  }

  bool saw_cancel_callback = false;
  std::vector<ava::provider::HttpRequest> requests;
};

void test_bash_tool()
{
  std::error_code remove_error;
  std::filesystem::remove_all(temp_root(), remove_error);
  std::filesystem::create_directories(temp_root());
  expect(::chmod(temp_root().c_str(), S_IRWXU) == 0, "bash fixture root is owner-only for sealed command planning");
  {
    std::ofstream visible(temp_root() / "visible-entry", std::ios::binary | std::ios::trunc);
    visible << "visible\n";
  }

  ava::tools::ToolContext const context{.workspace_dir = temp_root(), .mode = ava::agent::Mode::Build};

  auto pwd = ava::tools::run_bash(context, "ls", ava::tools::BashOptions{.timeout = std::chrono::milliseconds(1000)});
  expect(pwd.has_value(), "run_bash auto-allows a sealed standard inspection command");
  if (pwd)
  {
    expect(pwd->exit_code == 0, "run_bash records exit code");
    expect(pwd->output.find("visible-entry") != std::string::npos, "run_bash uses the sealed workspace directory");
  }

  auto capped_output = ava::tools::run_bash(context, "ls", ava::tools::BashOptions{.timeout = std::chrono::milliseconds(1000), .max_bytes = 4});
  expect(capped_output && capped_output->exit_code == 0 && capped_output->truncated && capped_output->byte_limited && capped_output->totals_known &&
             capped_output->output.size() == 4 && capped_output->output_bytes == capped_output->output.size() &&
             capped_output->total_bytes > capped_output->output.size(),
         "local run_bash bounds retained output while reporting exact total bytes");

  ava::tools::ToolContext const line_context{
      .workspace_dir = temp_root(),
      .mode = ava::agent::Mode::Build,
      .permission_resolver = [](ava::permissions::PermissionPrompt const&) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
        return ava::permissions::PermissionResolution::Allow;
      }};
  auto line_capped_output = ava::tools::run_bash(line_context, "seq 1 5", ava::tools::BashOptions{.timeout = std::chrono::milliseconds(1000), .max_lines = 2});
  expect(line_capped_output && line_capped_output->exit_code == 0 && line_capped_output->truncated && line_capped_output->line_limited &&
             !line_capped_output->byte_limited && line_capped_output->totals_known && line_capped_output->output == "4\n5\n" &&
             line_capped_output->total_lines == 5 && line_capped_output->output_lines == 2 && line_capped_output->omitted_lines == 3,
         "local run_bash retains output by line tail and reports exact totals before applying byte caps");

  std::vector<ava::tools::ToolProgressEvent> bash_progress;
  ava::tools::ToolContext const progress_context{.workspace_dir = temp_root(),
                                                 .mode = ava::agent::Mode::Build,
                                                 .progress_sink = [&bash_progress](ava::tools::ToolProgressEvent const& event) -> ava::core::VoidResult {
                                                   bash_progress.push_back(event);
                                                   return {};
                                                 },
                                                 .current_tool_name = "bash",
                                                 .current_call_id = "call_progress"};
  auto progress_pwd = ava::tools::run_bash(progress_context, "ls", ava::tools::BashOptions{.timeout = std::chrono::milliseconds(1000)});
  expect(progress_pwd && std::ranges::any_of(bash_progress,
                                             [](ava::tools::ToolProgressEvent const& event) {
                                               return event.call_id == "call_progress" && event.tool_name == "bash" && event.status == "completed";
                                             }),
         "run_bash emits bounded progress events with current call metadata");

  auto const bash_spill_dir = temp_root() / "session" / "bash-spill";
  ava::tools::ToolContext const bash_spill_context{
      .workspace_dir = temp_root(),
      .spill_dir = bash_spill_dir,
      .mode = ava::agent::Mode::Build,
      .permission_resolver = [](ava::permissions::PermissionPrompt const&) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
        return ava::permissions::PermissionResolution::Allow;
      },
      .current_tool_name = "bash",
      .current_call_id = "call/bash:spill"};
  auto bash_spill =
      ava::tools::run_bash(bash_spill_context, "printf 0123456789abcdef", ava::tools::BashOptions{.timeout = std::chrono::milliseconds(1000), .max_bytes = 4});
  expect(bash_spill && bash_spill->truncated && !bash_spill->spill_path.empty() && bash_spill->spill_path.parent_path() == bash_spill_dir &&
             std::filesystem::exists(bash_spill->spill_path),
         "run_bash spills truncated combined output under the configured spill directory");
  if (bash_spill && !bash_spill->spill_path.empty())
  {
    expect(read_text_file_for_test(bash_spill->spill_path) == "0123456789abcdef",
           "bash spill file contains raw combined output from the beginning of the stream");
  }

  auto capped_spill =
      ava::tools::run_bash(bash_spill_context, "seq 1 1000000", ava::tools::BashOptions{.timeout = std::chrono::milliseconds(5000), .max_bytes = 1});
  expect(capped_spill && capped_spill->truncated && capped_spill->spill_truncated && !capped_spill->spill_path.empty() &&
             std::filesystem::file_size(capped_spill->spill_path) == ava::tools::kMaxSpillFileBytes,
         "run_bash caps individual spill files and reports spill truncation");

  auto const hijack_path = temp_root() / "pwd";
  {
    std::ofstream hijack(hijack_path, std::ios::binary | std::ios::trunc);
    hijack << "#!/bin/sh\nprintf hijacked-path\n";
  }
  expect(chmod(hijack_path.c_str(), 0700) == 0, "test can create executable PATH hijack fixture");
  {
    ScopedEnvVar const path_guard("PATH", temp_root().string() + ":.:relative");
    auto sanitized_pwd = ava::tools::run_bash(context, "ls", ava::tools::BashOptions{.timeout = std::chrono::milliseconds(1000)});
    expect(!sanitized_pwd && sanitized_pwd.error().format().find("PATH") != std::string::npos,
           "run_bash rejects an unsafe inherited startup PATH before it can influence a sealed command");
  }

  auto denied = ava::tools::run_bash(context, "rm -rf important");
  expect(!denied, "run_bash denies destructive command");

  auto chained = ava::tools::run_bash(context, "pwd; rm -rf important");
  expect(!chained, "run_bash rejects shell metacharacters");
  auto allowed_prefix_chain = ava::tools::run_bash(context, "pwd; whoami");
  expect(!allowed_prefix_chain, "run_bash rejects shell metacharacters after allowed prefix");

  expect(!ava::tools::run_bash(context, "cmake -E cat ~/.config/ava/auth.json"), "run_bash denies cmake -E file helper before execution");
  expect(!ava::tools::run_bash(context, "cmake -P docs/plan.md"), "run_bash denies cmake script execution before execution");
  expect(!ava::tools::run_bash(context, "cmake -E copy docs/plan.md src/new.cpp"), "run_bash denies cmake copy helper before execution");

  auto const repository_test_dir = temp_root() / "repository-test-fixture";
  auto const repository_test_marker = repository_test_dir / "executed-marker";
  auto const safe_command_bin = temp_root() / "sealed-command-bin";
  std::filesystem::create_directories(repository_test_dir);
  std::filesystem::create_directories(safe_command_bin);
  expect(::chmod(repository_test_dir.c_str(), S_IRWXU) == 0 && ::chmod(safe_command_bin.c_str(), S_IRWXU) == 0,
         "repository command fixture paths are owner-only for sealed recipe planning");
  for (auto const name : {"cmake", "ctest"})
  {
    std::ofstream executable(safe_command_bin / name, std::ios::binary | std::ios::trunc);
    executable << "#!/bin/sh\nexit 0\n";
    executable.close();
    expect(::chmod((safe_command_bin / name).c_str(), S_IRUSR | S_IWUSR | S_IXUSR) == 0, "repository command fixture can create a sealed executable identity");
  }
  ScopedEnvVar const repository_path("PATH", safe_command_bin.string() + ":/usr/bin:/bin");
  {
    std::ofstream test_file(repository_test_dir / "CTestTestfile.cmake", std::ios::binary | std::ios::trunc);
    test_file << "add_test(NAME repository_controlled_code COMMAND /usr/bin/touch " << repository_test_marker.generic_string() << ")\n";
  }
  auto repository_build = ava::tools::run_bash(context, "cmake --build " + repository_test_dir.generic_string());
  expect(repository_build && repository_build->exit_code == 0 && !std::filesystem::exists(repository_test_marker),
         "repository builds auto-allow under verified development containment; the fake cmake exits without creating the test marker");

  auto repository_test = ava::tools::run_bash(context, "ctest --test-dir " + repository_test_dir.generic_string());
  expect(repository_test && repository_test->exit_code == 0 && !std::filesystem::exists(repository_test_marker),
         "repository tests auto-allow under verified development containment; the fake ctest exits without creating the test marker");

  ava::tools::ToolContext const timeout_context{
      .workspace_dir = temp_root(),
      .mode = ava::agent::Mode::Build,
      .permission_resolver = [](ava::permissions::PermissionPrompt const&) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
        return ava::permissions::PermissionResolution::Allow;
      }};
  auto timeout = ava::tools::run_bash(timeout_context, "sleep 2", ava::tools::BashOptions{.timeout = std::chrono::milliseconds(50)});
  expect(timeout && timeout->timed_out, "run_bash times out long command");

  auto const timeout_group_file = temp_root() / "bash-timeout-child-pgid.txt";
  std::filesystem::remove(timeout_group_file, remove_error);
  int timeout_tree_prompts = 0;
  ava::tools::ToolContext const timeout_tree_context{
      .workspace_dir = temp_root(),
      .mode = ava::agent::Mode::Build,
      .permission_resolver =
          [&timeout_tree_prompts](ava::permissions::PermissionPrompt const& prompt) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
        ++timeout_tree_prompts;
        expect(prompt.operation == ava::permissions::Operation::RunCommand, "bash process-tree timeout resolver receives run operation");
        return ava::permissions::PermissionResolution::Allow;
      }};
  auto timeout_tree = ava::tools::run_bash(timeout_tree_context, "/bin/sh -c \"sleep 30 & printf $$ > " + timeout_group_file.generic_string() + "; wait\"",
                                           ava::tools::BashOptions{.timeout = std::chrono::milliseconds(500), .max_bytes = 1024});
  auto const timeout_pgid = read_pid_file_for_test(timeout_group_file);
  expect(timeout_tree && timeout_tree->timed_out && timeout_tree_prompts == 1 && timeout_pgid && wait_for_process_group_exit(*timeout_pgid),
         "run_bash timeout terminates child processes in the command process group");

  int cancel_checks = 0;
  ava::tools::ToolContext const cancel_context{
      .workspace_dir = temp_root(),
      .mode = ava::agent::Mode::Build,
      .permission_resolver = [](ava::permissions::PermissionPrompt const&) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
        return ava::permissions::PermissionResolution::Allow;
      },
      .cancel_requested =
          [&cancel_checks] {
            ++cancel_checks;
            return cancel_checks >= 3;
          }};
  auto canceled = ava::tools::run_bash(cancel_context, "sleep 2", ava::tools::BashOptions{.timeout = std::chrono::milliseconds(5000)});
  expect(canceled && canceled->canceled && !canceled->timed_out && canceled->exit_code == -1,
         "run_bash observes tool cancellation and reports a canceled process result");

  auto const cancel_group_file = temp_root() / "bash-cancel-child-pgid.txt";
  std::filesystem::remove(cancel_group_file, remove_error);
  int cancel_tree_prompts = 0;
  ava::tools::ToolContext const cancel_tree_context{
      .workspace_dir = temp_root(),
      .mode = ava::agent::Mode::Build,
      .permission_resolver =
          [&cancel_tree_prompts](ava::permissions::PermissionPrompt const& prompt) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
        ++cancel_tree_prompts;
        expect(prompt.operation == ava::permissions::Operation::RunCommand, "bash process-tree cancel resolver receives run operation");
        return ava::permissions::PermissionResolution::Allow;
      },
      .cancel_requested = [&cancel_group_file] { return std::filesystem::exists(cancel_group_file); }};
  auto cancel_tree = ava::tools::run_bash(cancel_tree_context, "/bin/sh -c \"sleep 30 & printf $$ > " + cancel_group_file.generic_string() + "; wait\"",
                                          ava::tools::BashOptions{.timeout = std::chrono::milliseconds(5000), .max_bytes = 1024});
  auto const cancel_pgid = read_pid_file_for_test(cancel_group_file);
  expect(
      cancel_tree && cancel_tree->canceled && !cancel_tree->timed_out && cancel_tree_prompts == 1 && cancel_pgid && wait_for_process_group_exit(*cancel_pgid),
      "run_bash cancellation terminates child processes in the command process group");

  auto ask_without_resolver = ava::tools::run_bash(context, "true");
  expect(!ask_without_resolver && ask_without_resolver.error().format().find("resolution: no_resolver") != std::string::npos,
         "run_bash fails closed for ask decisions without a resolver");

  int bash_prompts = 0;
  ava::tools::ToolContext allow_context{
      .workspace_dir = temp_root(),
      .mode = ava::agent::Mode::Build,
      .permission_resolver =
          [&bash_prompts](ava::permissions::PermissionPrompt const& prompt) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
        ++bash_prompts;
        expect(prompt.operation == ava::permissions::Operation::RunCommand, "bash resolver receives run operation");
        expect(prompt.command == "true", "bash resolver receives command text");
        return ava::permissions::PermissionResolution::Allow;
      }};
  auto ask_allowed = ava::tools::run_bash(allow_context, "true");
  expect(ask_allowed && ask_allowed->exit_code == 0 && bash_prompts == 1, "run_bash allows ask decisions when resolver allows once");

  ava::tools::ToolContext deny_context{
      .workspace_dir = temp_root(),
      .mode = ava::agent::Mode::Build,
      .permission_resolver = [](ava::permissions::PermissionPrompt const&) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
        return ava::permissions::PermissionResolution::Deny;
      }};
  auto ask_denied = ava::tools::run_bash(deny_context, "true");
  expect(!ask_denied && ask_denied.error().format().find("resolution: deny") != std::string::npos, "run_bash fails closed when resolver denies ask decisions");

  ava::tools::ToolContext failing_context{
      .workspace_dir = temp_root(),
      .mode = ava::agent::Mode::Build,
      .permission_resolver = [](ava::permissions::PermissionPrompt const&) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
        return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "resolver failed"));
      }};
  auto ask_failed = ava::tools::run_bash(failing_context, "true");
  expect(!ask_failed && ask_failed.error().format().find("resolution: resolver_failed") != std::string::npos, "run_bash fails closed when resolver fails");
}

void test_injected_command_executor()
{
  auto const workspace = temp_root() / "injected-command-workspace";
  std::filesystem::create_directories(workspace);
  expect(::chmod(temp_root().c_str(), S_IRWXU) == 0 && ::chmod(workspace.c_str(), S_IRWXU) == 0,
         "injected command workspace is owner-only for sealed command planning");
  auto executor = std::make_shared<RecordingCommandExecutor>();
  executor->result = ava::tools::CommandExecutionResult{.exit_code = 0, .output = "one\ntwo\nthree\n"};
  int prompts = 0;
  std::string permission_fingerprint;
  ava::tools::ToolContext context{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .permission_resolver = [&prompts, &permission_fingerprint](
                                 ava::permissions::PermissionPrompt const& prompt) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
        ++prompts;
        expect(prompt.operation == ava::permissions::Operation::RunCommand, "injected command permission uses the command operation");
        expect(prompt.command_metadata && prompt.command_metadata->level == ava::command::CommandLevel::Critical &&
                   !prompt.command_metadata->executor_identity_verified &&
                   prompt.command_metadata->backend_maximum_scope == ava::command::InteractiveScope::Once,
               "injected command permission treats the executor as unverified and one-shot only");
        permission_fingerprint = prompt.command_metadata ? prompt.command_metadata->fingerprint : std::string{};
        return ava::permissions::PermissionResolution::Allow;
      },
      .command_executor = executor};
  auto result =
      ava::tools::run_bash(context, "printf 'one two'", ava::tools::BashOptions{.timeout = std::chrono::milliseconds(900), .max_bytes = 8, .max_lines = 2});
  expect(result && prompts == 1 && executor->requests.size() == 1 && executor->requests.front().argv.size() == 3 &&
             executor->requests.front().argv[1] == "-c" && executor->requests.front().argv[2] == "printf 'one two'" &&
             executor->requests.front().cwd == std::filesystem::canonical(workspace) && executor->requests.front().timeout == std::chrono::milliseconds(900) &&
             executor->requests.front().output_byte_limit == 8 && executor->requests.front().plan_metadata &&
             executor->requests.front().plan_metadata->fingerprint.starts_with("sha256:ava-command-plan-") &&
             executor->requests.front().plan_metadata->fingerprint == permission_fingerprint &&
             !executor->requests.front().plan_metadata->executor_identity_verified && executor->requests.front().environment_profile &&
             !executor->requests.front().environment_profile->local_execution_authority && result->line_limited && result->totals_known &&
             result->total_lines == 3 && result->total_bytes == executor->result.output.size() && result->output == "three\n",
         "unverified injected execution receives the raw one-shot plan fingerprint and redacted environment contract while local bounds retain exact totals");

  ava::tools::ToolContext denied = context;
  denied.permission_resolver = [](ava::permissions::PermissionPrompt const&) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
    return ava::permissions::PermissionResolution::Deny;
  };
  auto denied_result = ava::tools::run_bash(denied, "touch denied-marker");
  auto raw_shell = ava::tools::run_bash(context, "printf ok; touch denied-marker");
  expect(!denied_result && raw_shell && executor->requests.size() == 2 && executor->requests.back().argv.size() == 3 &&
             executor->requests.back().argv[1] == "-c" && executor->requests.back().argv[2] == "printf ok; touch denied-marker" &&
             !std::filesystem::exists(workspace / "denied-marker"),
         "permission denial blocks callbacks, while an approved raw shell plan reaches the injected executor without local execution");

  executor->fail = true;
  auto failed = ava::tools::run_bash(context, "true");
  expect(!failed && failed.error().message() == "injected command execution failed",
         "an installed command executor failure never falls back to local fork/exec");
}

void test_sealed_local_bash_contract()
{
  auto const root = temp_root() / "sealed-local-bash-contract";
  auto const workspace = root / "workspace";
  auto const first_bin = root / "first-bin";
  auto const second_bin = root / "second-bin";
  std::error_code cleanup;
  std::filesystem::remove_all(root, cleanup);
  std::filesystem::create_directories(workspace);
  std::filesystem::create_directories(first_bin);
  std::filesystem::create_directories(second_bin);
  expect(::chmod(temp_root().c_str(), S_IRWXU) == 0 && ::chmod(root.c_str(), S_IRWXU) == 0 && ::chmod(workspace.c_str(), S_IRWXU) == 0 &&
             ::chmod(first_bin.c_str(), S_IRWXU) == 0 && ::chmod(second_bin.c_str(), S_IRWXU) == 0,
         "sealed local bash fixture owns every planning root");
  auto write_executable = [](std::filesystem::path const& path, std::string_view body) {
    std::ofstream executable(path, std::ios::binary | std::ios::trunc);
    executable << body;
    executable.close();
    return ::chmod(path.c_str(), S_IRUSR | S_IWUSR | S_IXUSR) == 0;
  };
  expect(write_executable(first_bin / "inspect",
                          "#!/bin/sh\nprintf 'argc=%s first=<%s> second=<%s>\\n' \"$#\" \"$1\" \"$2\"\n"
                          "if [ -z \"${AVA_BASH_SECRET_SENTINEL+x}\" ]; then printf 'sentinels=absent\\n'; else printf 'sentinels=present\\n'; fi\n"
                          "if IFS= read -r line; then printf 'stdin=data\\n'; else printf 'stdin=eof\\n'; fi\n") &&
             write_executable(first_bin / "chosen", "#!/bin/sh\nprintf 'first-choice\\n'\n") &&
             write_executable(second_bin / "chosen", "#!/bin/sh\nprintf 'second-choice\\n'\n") &&
             write_executable(first_bin / "stale", "#!/bin/sh\nprintf old-stale\\n") && write_executable(first_bin / "npm", "#!/bin/sh\nprintf npm-ran\\n") &&
             write_executable(first_bin / "python", "#!/bin/sh\nprintf python-ran\\n") &&
             write_executable(first_bin / "python3", "#!/bin/sh\nprintf 'env-python-ran\\n'\n") &&
             write_executable(first_bin / "env-python", "#!/usr/bin/env python3\nprintf 'script-body-should-not-run-directly\\n'\n") &&
             write_executable(first_bin / "bash", "#!/bin/sh\nprintf bash-ran\\n") && write_executable(first_bin / "rm", "#!/bin/sh\nprintf rm-ran\\n"),
         "sealed local bash fixture creates executable identities");

  ScopedEnvVar const path_guard("PATH", first_bin.string() + ":/usr/bin:/bin");
  ScopedEnvVar const sentinel_guard("AVA_BASH_SECRET_SENTINEL", "must-not-reach-child");
  std::vector<ava::permissions::PermissionPrompt> prompts;
  std::vector<ava::tools::PermissionAuditEvent> audits;
  ava::tools::ToolContext const context{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .permission_resolver = [&prompts](ava::permissions::PermissionPrompt const& prompt) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
        prompts.push_back(prompt);
        return ava::permissions::PermissionResolution::Allow;
      },
      .permission_audit_sink = [&audits](ava::tools::PermissionAuditEvent const& event) -> ava::core::VoidResult {
        audits.push_back(event);
        return {};
      }};

  auto const rule_store = ava::permissions::PermissionRuleStore{
      .global_rules_file = root / "permission-rules.json", .workspace_rules_file = workspace / ".ava" / "permission-rules.json", .workspace_dir = workspace};
  auto deny_ls =
      ava::permissions::add_persistent_permission_rule(rule_store, ava::permissions::PermissionRuleDraft{.scope = ava::permissions::PermissionRuleScope::Global,
                                                                                                         .action = ava::permissions::PermissionAction::Deny,
                                                                                                         .operation = ava::permissions::Operation::RunCommand,
                                                                                                         .mode = ava::permissions::PermissionRuleMode::Build,
                                                                                                         .tool_name = "bash",
                                                                                                         .target_path = {},
                                                                                                         .command = "ls",
                                                                                                         .reason = "test operator deny",
                                                                                                         .actor = "test"});
  auto denied_context = context;
  denied_context.command_deny_preflight = ava::permissions::build_persistent_permission_deny_preflight(rule_store);
  auto denied_inspection = ava::tools::run_bash(denied_context, "ls", ava::tools::BashOptions{.timeout = std::chrono::milliseconds(1000)});
  expect(deny_ls && !denied_inspection && prompts.empty(),
         "an explicit persistent deny remains authoritative before a standard inspection command can auto-run");
  audits.clear();

  auto exact = ava::tools::run_bash(context, "inspect '' second", ava::tools::BashOptions{.timeout = std::chrono::milliseconds(1000)});
  auto audit_json = audits.empty() ? std::string{} : ava::tools::permission_audit_data_json(audits.front());
  std::array<std::string_view, 10> const audit_fields{
      "\"level\"",  "\"family\"", "\"fingerprint\"",           "\"execution_domain\"",   "\"resolved_executable\"",
      "\"origin\"", "\"cwd\"",    "\"containment_available\"", "\"containment_status\"", "\"backend_maximum_scope\""};
  bool const complete_audit_metadata =
      std::ranges::all_of(audit_fields, [&audit_json](std::string_view field) { return audit_json.find(field) != std::string::npos; });
  auto env_python = ava::tools::run_bash(context, "env-python", ava::tools::BashOptions{.timeout = std::chrono::milliseconds(1000)});
  expect(exact && exact->output.find("argc=2 first=<> second=<second>") != std::string::npos && exact->output.find("sentinels=absent") != std::string::npos &&
             exact->output.find("stdin=eof") != std::string::npos && env_python && env_python->output == "env-python-ran\n" && prompts.size() == 2 &&
             prompts.front().command_metadata && !audits.empty() && audits.front().command_metadata &&
             prompts.front().command_metadata->fingerprint == audits.front().command_metadata->fingerprint && complete_audit_metadata &&
             audit_json.find("must-not-reach-child") == std::string::npos && audit_json.find("AVA_BASH_SECRET_SENTINEL") == std::string::npos,
         "one sealed plan carries exact empty argv, stdin EOF, redacted environment, and the same fingerprint through prompt and audit");

  ava::tools::ToolContext no_relookup_context = context;
  no_relookup_context.permission_resolver =
      [&second_bin](ava::permissions::PermissionPrompt const&) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
    static_cast<void>(::setenv("PATH", (second_bin.string() + ":/usr/bin:/bin").c_str(), 1));
    return ava::permissions::PermissionResolution::Allow;
  };
  auto no_relookup = ava::tools::run_bash(no_relookup_context, "chosen", ava::tools::BashOptions{.timeout = std::chrono::milliseconds(1000)});
  expect(no_relookup && no_relookup->output == "first-choice\n", "local execution uses the prepared executable without a post-permission PATH lookup");

  static_cast<void>(::setenv("PATH", (first_bin.string() + ":/usr/bin:/bin").c_str(), 1));
  ava::tools::ToolContext stale_context = context;
  stale_context.permission_resolver =
      [&first_bin, &write_executable](ava::permissions::PermissionPrompt const&) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
    return write_executable(first_bin / "stale", "#!/bin/sh\nprintf replacement\\n")
               ? ava::core::Result<ava::permissions::PermissionResolutionDecision>(ava::permissions::PermissionResolution::Allow)
               : ava::core::Result<ava::permissions::PermissionResolutionDecision>(
                     std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "failed to replace stale executable")));
  };
  auto stale = ava::tools::run_bash(stale_context, "stale", ava::tools::BashOptions{.timeout = std::chrono::milliseconds(1000)});
  expect(!stale && stale.error().format().find("stale") != std::string::npos, "stale executable identity blocks execution after permission");

  std::vector<ava::permissions::PermissionPrompt> critical_prompts;
  ava::tools::ToolContext deny_critical_context{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .permission_resolver =
          [&critical_prompts](ava::permissions::PermissionPrompt const& prompt) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
        critical_prompts.push_back(prompt);
        return ava::permissions::PermissionResolution::Deny;
      }};
  auto npm = ava::tools::run_bash(ava::tools::ToolContext{.workspace_dir = workspace, .mode = ava::agent::Mode::Build}, "npm run test");
  auto python_bare = ava::tools::run_bash(deny_critical_context, "python -c 'print(1)'");
  auto python_absolute = ava::tools::run_bash(deny_critical_context, (first_bin / "python").string() + " -c 'print(1)'");
  auto bash_inline = ava::tools::run_bash(deny_critical_context, "bash -c true");
  auto destructive = ava::tools::run_bash(deny_critical_context, "rm -rf remove-me");
  auto raw = ava::tools::run_bash(deny_critical_context, "printf raw; true");
  bool const all_critical = critical_prompts.size() == 5 && std::ranges::all_of(critical_prompts, [](ava::permissions::PermissionPrompt const& prompt) {
                              return prompt.command_metadata && prompt.command_metadata->level == ava::command::CommandLevel::Critical &&
                                     prompt.command_metadata->backend_maximum_scope == ava::command::InteractiveScope::Once;
                            });
  expect(npm && npm->exit_code == 0 && npm->output.find("npm-ran") != std::string::npos && !python_bare && !python_absolute && !bash_inline && !destructive &&
             !raw && all_critical && critical_prompts[0].command_metadata->family == critical_prompts[1].command_metadata->family &&
             critical_prompts[0].command_metadata->resolved_executable == critical_prompts[1].command_metadata->resolved_executable,
         "npm project code auto-allows under verified development containment, while bare/absolute Python, bash, destructive, and raw-shell commands remain "
         "critical one-shot prompts");

  auto const normal_group_file = workspace / "normal-background-pgid";
  ava::tools::ToolContext const group_context{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .permission_resolver = [](ava::permissions::PermissionPrompt const&) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
        return ava::permissions::PermissionResolution::Allow;
      }};
  auto normal_group = ava::tools::run_bash(group_context, "sleep 30 & printf $$ > " + normal_group_file.string(),
                                           ava::tools::BashOptions{.timeout = std::chrono::milliseconds(1000)});
  auto const normal_pgid = read_pid_file_for_test(normal_group_file);
  expect(normal_group && normal_group->exit_code == 0 && normal_pgid && wait_for_process_group_exit(*normal_pgid),
         "normal leader completion cleans verified background children before returning");
}

void test_sealed_process_group_sentinel_and_grace()
{
  auto const root = temp_root() / "sealed-process-group-sentinel-grace";
  std::error_code cleanup;
  std::filesystem::remove_all(root, cleanup);
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  expect(::chmod(temp_root().c_str(), S_IRWXU) == 0 && ::chmod(root.c_str(), S_IRWXU) == 0 && ::chmod(workspace.c_str(), S_IRWXU) == 0,
         "sentinel/grace fixture owns every root");
  auto const first_bin = root / "bin";
  std::filesystem::create_directories(first_bin);
  expect(::chmod(first_bin.c_str(), S_IRWXU) == 0, "sentinel/grace fixture bin is owner-only");
  auto write_executable = [](std::filesystem::path const& path, std::string_view body) {
    std::ofstream executable(path, std::ios::binary | std::ios::trunc);
    executable << body;
    executable.close();
    return ::chmod(path.c_str(), S_IRUSR | S_IWUSR | S_IXUSR) == 0;
  };

  // A fixture that verifies the leader has no unexpected child processes.
  // If the sentinel were forked by the leader (old design), it would appear
  // here as an unexpected child. With the AVA-owned sibling design, the
  // leader's /proc/self/task/$$/children is empty.
  expect(write_executable(first_bin / "child-check",
                          "#!/bin/sh\n"
                          "children=''\n"
                          "while IFS= read -r line; do\n"
                          "  children=\"${children}${line} \"\n"
                          "done < /proc/self/task/$$/children 2>/dev/null\n"
                          "if [ -n \"$children\" ]; then\n"
                          "  printf 'unexpected-children: %s\\n' \"$children\"\n"
                          "else\n"
                          "  printf 'no-unexpected-children\\n'\n"
                          "fi\n"),
         "sentinel/grace fixture creates the child-check executable");

  // A fixture that starts a background subshell with a SIGTERM handler that
  // writes a marker after a short delay, then the leader exits normally.
  auto const grace_marker = workspace / "grace-marker";
  std::filesystem::remove(grace_marker, cleanup);
  expect(write_executable(first_bin / "grace-leader",
                          "#!/bin/sh\n"
                          "(\n"
                          "  trap 'sleep 0.1; touch " +
                              grace_marker.string() +
                              "; exit 0' TERM\n"
                              "  while true; do sleep 0.05; done\n"
                              ") &\n"
                              "exit 0\n"),
         "sentinel/grace fixture creates the grace-leader executable");

  ScopedEnvVar const path_guard("PATH", first_bin.string() + ":/usr/bin:/bin");
  ava::tools::ToolContext const context{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .permission_resolver = [](ava::permissions::PermissionPrompt const&) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
        return ava::permissions::PermissionResolution::Allow;
      }};

  struct sigaction previous_term_action{};
  struct sigaction ignored_term_action{};
  ignored_term_action.sa_handler = SIG_IGN;
  static_cast<void>(::sigemptyset(&ignored_term_action.sa_mask));
  bool const term_action_installed = ::sigaction(SIGTERM, &ignored_term_action, &previous_term_action) == 0;
  sigset_t blocked_term{};
  sigset_t previous_mask{};
  static_cast<void>(::sigemptyset(&blocked_term));
  static_cast<void>(::sigaddset(&blocked_term, SIGTERM));
  bool const term_mask_installed = term_action_installed && ::sigprocmask(SIG_BLOCK, &blocked_term, &previous_mask) == 0;
  expect(term_mask_installed, "sentinel/grace fixture installs inherited ignored and blocked SIGTERM state");

  // Fix 1: the sentinel is an AVA-owned sibling, so the leader (after exec)
  // has no unexpected child processes.
  auto child_check = ava::tools::run_bash(context, "child-check", ava::tools::BashOptions{.timeout = std::chrono::milliseconds(2000)});
  expect(child_check && child_check->exit_code == 0 && child_check->output.find("no-unexpected-children") != std::string::npos,
         "the sealed command sentinel is an AVA-owned sibling: a direct command sees no sentinel child");

  // Fix 2: the verified process group receives a finite SIGTERM grace period
  // even when the leader has already exited normally. The background child's
  // TERM handler completes during grace, and no SIGKILL is needed.
  auto grace = ava::tools::run_bash(context, "grace-leader", ava::tools::BashOptions{.timeout = std::chrono::milliseconds(2000)});
  expect(grace && grace->exit_code == 0 && std::filesystem::exists(grace_marker),
         "background child TERM handler completes during the finite grace period on normal leader exit");

  // Timeout coverage: a long-running command with a background child is
  // terminated via the timeout path, which also applies the grace period.
  auto const timeout_marker = workspace / "timeout-marker";
  std::filesystem::remove(timeout_marker, cleanup);
  auto const timeout_group_file = workspace / "timeout-child-pgid";
  std::filesystem::remove(timeout_group_file, cleanup);
  expect(write_executable(first_bin / "timeout-leader",
                          "#!/bin/sh\n"
                          "(\n"
                          "  trap 'sleep 0.1; touch " +
                              timeout_marker.string() +
                              "; exit 0' TERM\n"
                              "  printf $$ > " +
                              timeout_group_file.string() +
                              "\n"
                              "  while true; do sleep 0.05; done\n"
                              ") &\n"
                              "while true; do sleep 1; done\n"),
         "sentinel/grace fixture creates the timeout-leader executable");
  auto timeout_result = ava::tools::run_bash(context, "timeout-leader", ava::tools::BashOptions{.timeout = std::chrono::milliseconds(500), .max_bytes = 1024});
  auto const timeout_pgid = read_pid_file_for_test(timeout_group_file);
  expect(timeout_result && timeout_result->timed_out && timeout_pgid && wait_for_process_group_exit(*timeout_pgid) && std::filesystem::exists(timeout_marker),
         "timeout termination applies the grace period to the verified process group");

  // Cancel coverage: cancellation also applies the grace period.
  auto const cancel_marker = workspace / "cancel-marker";
  std::filesystem::remove(cancel_marker, cleanup);
  auto const cancel_group_file = workspace / "cancel-child-pgid";
  std::filesystem::remove(cancel_group_file, cleanup);
  expect(write_executable(first_bin / "cancel-leader",
                          "#!/bin/sh\n"
                          "(\n"
                          "  trap 'sleep 0.1; touch " +
                              cancel_marker.string() +
                              "; exit 0' TERM\n"
                              "  printf $$ > " +
                              cancel_group_file.string() +
                              "\n"
                              "  while true; do sleep 0.05; done\n"
                              ") &\n"
                              "while true; do sleep 1; done\n"),
         "sentinel/grace fixture creates the cancel-leader executable");
  ava::tools::ToolContext const cancel_context{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .permission_resolver = [](ava::permissions::PermissionPrompt const&) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
        return ava::permissions::PermissionResolution::Allow;
      },
      .cancel_requested = [&cancel_group_file] { return std::filesystem::exists(cancel_group_file); }};
  auto cancel_result =
      ava::tools::run_bash(cancel_context, "cancel-leader", ava::tools::BashOptions{.timeout = std::chrono::milliseconds(5000), .max_bytes = 1024});
  auto const cancel_pgid = read_pid_file_for_test(cancel_group_file);
  expect(cancel_result && cancel_result->canceled && !cancel_result->timed_out && cancel_pgid && wait_for_process_group_exit(*cancel_pgid) &&
             std::filesystem::exists(cancel_marker),
         "cancellation applies the grace period to the verified process group");

  if (term_mask_installed)
    static_cast<void>(::sigprocmask(SIG_SETMASK, &previous_mask, nullptr));
  if (term_action_installed)
    static_cast<void>(::sigaction(SIGTERM, &previous_term_action, nullptr));
}

void test_webfetch_tool()
{
  std::error_code remove_error;
  std::filesystem::remove_all(temp_root(), remove_error);
  std::filesystem::create_directories(temp_root());
  auto const workspace = temp_root() / "webfetch-workspace";
  std::filesystem::create_directories(workspace);

  StaticTransport transport(ava::provider::HttpResponse{.status_code = 200, .headers = {{"content-type", "text/plain; charset=utf-8"}}, .body = "abcdef"});
  int prompts = 0;
  ava::tools::ToolContext const context{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .permission_resolver = [&prompts](ava::permissions::PermissionPrompt const& prompt) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
        ++prompts;
        expect(prompt.operation == ava::permissions::Operation::NetworkFetch, "webfetch resolver receives network operation");
        expect(prompt.command == "https://example.com/page", "webfetch resolver receives URL as command target");
        return ava::permissions::PermissionResolution::Allow;
      }};
  auto fetched =
      ava::tools::webfetch(context, "https://example.com/page", ava::tools::WebFetchOptions{.max_bytes = 3, .timeout_ms = 5000, .transport = &transport});
  expect(fetched && fetched->content == "abc" && fetched->truncated && fetched->byte_limited && fetched->total_bytes == 6 && fetched->output_bytes == 3 &&
             fetched->output_lines == 1 && fetched->content_type == "text/plain; charset=utf-8" && prompts == 1 && transport.requests.size() == 1 &&
             transport.requests[0].method == "GET" && transport.requests[0].timeout_ms == 5000 && !transport.requests[0].follow_redirects &&
             transport.requests[0].include_response_headers,
         "webfetch requires permission and bounds fetched text content");

  StaticTransport multiline_transport(
      ava::provider::HttpResponse{.status_code = 200, .headers = {{"content-type", "text/plain; charset=utf-8"}}, .body = "one\ntwo\nthree\nfour\n"});
  auto fetched_lines = ava::tools::webfetch(
      context, "https://example.com/page",
      ava::tools::WebFetchOptions{.max_bytes = 1024, .offset_line = 2, .max_lines = 2, .timeout_ms = 5000, .transport = &multiline_transport});
  expect(fetched_lines && fetched_lines->content == "two\nthree\n" && fetched_lines->line_limited && !fetched_lines->byte_limited &&
             fetched_lines->output_lines == 2 && fetched_lines->start_line == 2 && fetched_lines->end_line == 3 && fetched_lines->total_lines == 4 &&
             fetched_lines->next_offset_line == 4,
         "webfetch supports line offset and limit continuation metadata");

  StaticTransport html_transport(ava::provider::HttpResponse{.status_code = 200,
                                                             .headers = {{"content-type", "text/html; charset=utf-8"}},
                                                             .body = "<html><body><h1>Title</h1><script>hidden()</script><p>A&amp;B</p></body></html>"});
  auto html_text = ava::tools::webfetch(
      context, "https://example.com/page",
      ava::tools::WebFetchOptions{.max_bytes = 1024, .timeout_ms = 5000, .format = ava::tools::WebFetchFormat::Text, .transport = &html_transport});
  expect(html_text && html_text->content.find("Title") != std::string::npos && html_text->content.find("A&B") != std::string::npos &&
             html_text->content.find("hidden") == std::string::npos && html_transport.requests[0].headers.at("Accept").find("text/plain") != std::string::npos,
         "webfetch supports text output for HTML responses with basic tag stripping");

  StaticTransport unused_transport(ava::provider::HttpResponse{.status_code = 200, .headers = {}, .body = "unused"});
  auto invalid_scheme = ava::tools::webfetch(context, "file:///etc/passwd", ava::tools::WebFetchOptions{.transport = &unused_transport});
  expect(!invalid_scheme && unused_transport.requests.empty(), "webfetch rejects non-http URLs before transport use");

  auto private_ip = ava::tools::webfetch(context, "http://127.0.0.1:8080", ava::tools::WebFetchOptions{.transport = &unused_transport});
  expect(!private_ip && unused_transport.requests.empty(), "webfetch rejects local IP hosts before transport use");

  auto short_ipv4 = ava::tools::webfetch(context, "http://127.1:8080", ava::tools::WebFetchOptions{.transport = &unused_transport});
  expect(!short_ipv4 && unused_transport.requests.empty(), "webfetch rejects shortened IPv4 literal hosts");

  auto decimal_ipv4 = ava::tools::webfetch(context, "http://2130706433/", ava::tools::WebFetchOptions{.transport = &unused_transport});
  expect(!decimal_ipv4 && unused_transport.requests.empty(), "webfetch rejects decimal IPv4 literal hosts");

  auto hex_ipv4 = ava::tools::webfetch(context, "http://0x7f000001/", ava::tools::WebFetchOptions{.transport = &unused_transport});
  expect(!hex_ipv4 && unused_transport.requests.empty(), "webfetch rejects hexadecimal IPv4 literal hosts");

  StaticTransport digit_domain_transport(ava::provider::HttpResponse{.status_code = 200, .headers = {}, .body = "domain ok"});
  ava::tools::ToolContext const permissive_network_context{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .permission_resolver = [](ava::permissions::PermissionPrompt const& prompt) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
        expect(prompt.operation == ava::permissions::Operation::NetworkFetch, "digit-leading domain still requests network permission");
        return ava::permissions::PermissionResolution::Allow;
      }};
  auto digit_domain = ava::tools::webfetch(permissive_network_context, "https://1.be/", ava::tools::WebFetchOptions{.transport = &digit_domain_transport});
  expect(digit_domain && digit_domain->content == "domain ok", "webfetch allows digit-leading DNS names that are not IP aliases");

  ava::tools::ToolContext const no_resolver_context{.workspace_dir = workspace, .mode = ava::agent::Mode::Build};
  auto no_resolver = ava::tools::webfetch(no_resolver_context, "https://example.com/page", ava::tools::WebFetchOptions{.transport = &unused_transport});
  expect(!no_resolver && no_resolver.error().format().find("no_resolver") != std::string::npos && unused_transport.requests.empty(),
         "webfetch fails closed without network permission resolver");

  ava::tools::ToolContext const deny_context{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .permission_resolver = [](ava::permissions::PermissionPrompt const& prompt) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
        expect(prompt.operation == ava::permissions::Operation::NetworkFetch, "webfetch deny resolver receives network fetch operation");
        return ava::permissions::PermissionResolution::Deny;
      }};
  auto denied_fetch = ava::tools::webfetch(deny_context, "https://example.com/page", ava::tools::WebFetchOptions{.transport = &unused_transport});
  expect(!denied_fetch && denied_fetch.error().format().find("resolution: deny") != std::string::npos && unused_transport.requests.empty(),
         "webfetch fails closed when network permission resolver denies before transport use");

  StaticTransport binary_transport(ava::provider::HttpResponse{.status_code = 200, .headers = {{"content-type", "application/octet-stream"}}, .body = "abc"});
  auto binary = ava::tools::webfetch(context, "https://example.com/page", ava::tools::WebFetchOptions{.transport = &binary_transport});
  expect(!binary && binary.error().message().find("binary") != std::string::npos, "webfetch rejects binary response content types");

  CancelAwareTransport cancel_aware_transport;
  int cancel_checks = 0;
  ava::tools::ToolContext const cancel_during_transport_context{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .permission_resolver = [](ava::permissions::PermissionPrompt const&) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
        return ava::permissions::PermissionResolution::Allow;
      },
      .cancel_requested =
          [&cancel_checks] {
            ++cancel_checks;
            return cancel_checks >= 3;
          },
  };
  auto canceled_fetch =
      ava::tools::webfetch(cancel_during_transport_context, "https://example.com/page", ava::tools::WebFetchOptions{.transport = &cancel_aware_transport});
  expect(!canceled_fetch && canceled_fetch.error().message().find("canceled") != std::string::npos && cancel_aware_transport.saw_cancel_callback &&
             cancel_aware_transport.requests.size() == 1,
         "webfetch passes cancellation into the transport request boundary");
}

void test_websearch_tool()
{
  std::error_code remove_error;
  std::filesystem::remove_all(temp_root(), remove_error);
  auto const workspace = temp_root() / "websearch-workspace";
  std::filesystem::create_directories(workspace);

  StaticTransport transport(
      ava::provider::HttpResponse{.status_code = 200,
                                  .headers = {{"content-type", "application/json"}},
                                  .body = "{\"Heading\":\"AVA\",\"AbstractText\":\"Native C++ agent\",\"AbstractURL\":\"https://ava.example/\","
                                          "\"RelatedTopics\":[{\"Text\":\"AVA docs\",\"FirstURL\":\"https://ava.example/docs\"},"
                                          "{\"Topics\":[{\"Text\":\"AVA releases\",\"FirstURL\":\"https://ava.example/releases\"}]}]}"});
  int prompts = 0;
  ava::tools::ToolContext const context{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .permission_resolver = [&prompts](ava::permissions::PermissionPrompt const& prompt) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
        ++prompts;
        expect(prompt.operation == ava::permissions::Operation::NetworkSearch, "websearch resolver receives network search operation");
        expect(prompt.command == "ava agent", "websearch resolver receives query as command target");
        return ava::permissions::PermissionResolution::Allow;
      }};

  auto searched = ava::tools::websearch(
      context, " ava agent ", ava::tools::WebSearchOptions{.max_results = 2, .context_max_chars = 10000, .timeout_ms = 7000, .transport = &transport});
  expect(searched && searched->query == "ava agent" && searched->results.size() == 2 && searched->results[0].title == "AVA" &&
             searched->results[1].url == "https://ava.example/docs" && searched->total_results == 3 && searched->truncated && prompts == 1 &&
             transport.requests.size() == 1 && transport.requests[0].method == "GET" && transport.requests[0].url.find("q=ava+agent") != std::string::npos &&
             transport.requests[0].timeout_ms == 7000 && transport.requests[0].follow_redirects,
         "websearch requires permission, queries a bounded search endpoint, and parses structured results");

  StaticTransport unused_transport(ava::provider::HttpResponse{.status_code = 200, .headers = {}, .body = "{}"});
  auto invalid_query = ava::tools::websearch(context, "\n", ava::tools::WebSearchOptions{.transport = &unused_transport});
  expect(!invalid_query && unused_transport.requests.empty(), "websearch rejects empty/control queries before transport use");

  ava::tools::ToolContext const no_resolver_context{.workspace_dir = workspace, .mode = ava::agent::Mode::Build};
  auto no_resolver = ava::tools::websearch(no_resolver_context, "ava agent", ava::tools::WebSearchOptions{.transport = &unused_transport});
  expect(!no_resolver && no_resolver.error().format().find("no_resolver") != std::string::npos && unused_transport.requests.empty(),
         "websearch fails closed without network search permission resolver");

  ava::tools::ToolContext const deny_context{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .permission_resolver = [](ava::permissions::PermissionPrompt const& prompt) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
        expect(prompt.operation == ava::permissions::Operation::NetworkSearch, "websearch deny resolver receives network search operation");
        return ava::permissions::PermissionResolution::Deny;
      }};
  auto denied_search = ava::tools::websearch(deny_context, "ava agent", ava::tools::WebSearchOptions{.transport = &unused_transport});
  expect(!denied_search && denied_search.error().format().find("resolution: deny") != std::string::npos && unused_transport.requests.empty(),
         "websearch fails closed when network search permission resolver denies before transport use");
}
}  // namespace

void run_tools_process_network_tests()
{
  test_bash_tool();
  test_injected_command_executor();
  test_sealed_local_bash_contract();
  test_sealed_process_group_sentinel_and_grace();
  test_webfetch_tool();
  test_websearch_tool();
}
