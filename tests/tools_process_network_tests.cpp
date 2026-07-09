#include "sys.h"
#include "ava/agent/mode.h"

#include "ava/tools/bash_tool.h"
#include "ava/tools/spill_files.h"
#include "ava/tools/webfetch_tool.h"
#include "ava/tools/websearch_tool.h"

#include "ava/permissions/permission.h"

#include "ava/provider/provider.h"

#include "ava/core/error.h"

#include "tests/support/test_harness.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <filesystem>
#include <fstream>
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
  if (!file || value <= 0) return std::nullopt;
  return static_cast<pid_t>(value);
}

bool process_group_exists(pid_t pgid)
{
  errno = 0;
  if (::kill(-pgid, 0) == 0) return true;
  return errno != ESRCH;
}

bool wait_for_process_group_exit(pid_t pgid)
{
  for (int index = 0; index < 100; ++index) {
    if (!process_group_exists(pgid)) return true;
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return !process_group_exists(pgid);
}

class StaticTransport final : public ava::provider::Transport {
 public:
  explicit StaticTransport(ava::provider::HttpResponse response) : response_(std::move(response)) {}

  [[nodiscard]] ava::core::Result<ava::provider::HttpResponse> send(ava::provider::HttpRequest const& request) override
  {
    requests.push_back(request);
    return response_;
  }

  std::vector<ava::provider::HttpRequest> requests;

 private:
  ava::provider::HttpResponse response_;
};

class CancelAwareTransport final : public ava::provider::Transport {
 public:
  [[nodiscard]] ava::core::Result<ava::provider::HttpResponse> send(ava::provider::HttpRequest const& request) override
  {
    requests.push_back(request);
    return ava::provider::HttpResponse{.status_code = 200, .headers = {{"content-type", "text/plain"}}, .body = "ok"};
  }

  [[nodiscard]] ava::core::Result<ava::provider::HttpResponse> send(ava::provider::HttpRequest const& request,
                                                                    CancelCallback cancel_requested) override
  {
    requests.push_back(request);
    saw_cancel_callback = static_cast<bool>(cancel_requested);
    if (cancel_requested && cancel_requested()) {
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

  ava::tools::ToolContext const context{.workspace_dir = temp_root(), .mode = ava::agent::Mode::Build};

  auto pwd = ava::tools::run_bash(context, "pwd", ava::tools::BashOptions{.timeout = std::chrono::milliseconds(1000)});
  expect(pwd.has_value(), "run_bash allows safe command");
  if (pwd) {
    expect(pwd->exit_code == 0, "run_bash records exit code");
    expect(pwd->output.find(temp_root().string()) != std::string::npos, "run_bash uses workspace directory");
  }

  auto capped_output = ava::tools::run_bash(
      context, "pwd", ava::tools::BashOptions{.timeout = std::chrono::milliseconds(1000), .max_bytes = 4});
  expect(capped_output && capped_output->exit_code == 0 && capped_output->truncated && capped_output->byte_limited &&
             capped_output->output.size() == 4 && capped_output->output_bytes == capped_output->output.size() &&
             capped_output->total_bytes > capped_output->output.size(),
         "run_bash bounds retained output while reporting total bytes");

  ava::tools::ToolContext const line_context{.workspace_dir = temp_root(),
                                             .mode = ava::agent::Mode::Build,
                                             .permission_resolver = [](ava::permissions::PermissionPrompt const&)
                                                 -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
                                               return ava::permissions::PermissionResolution::Allow;
                                             }};
  auto line_capped_output = ava::tools::run_bash(
      line_context, "seq 1 5", ava::tools::BashOptions{.timeout = std::chrono::milliseconds(1000), .max_lines = 2});
  expect(line_capped_output && line_capped_output->exit_code == 0 && line_capped_output->truncated &&
             line_capped_output->line_limited && !line_capped_output->byte_limited &&
             line_capped_output->output == "4\n5\n" && line_capped_output->total_lines == 5 &&
             line_capped_output->output_lines == 2 && line_capped_output->omitted_lines == 3,
         "run_bash retains output by line tail before applying byte caps");

  std::vector<ava::tools::ToolProgressEvent> bash_progress;
  ava::tools::ToolContext const progress_context{
      .workspace_dir = temp_root(),
      .mode = ava::agent::Mode::Build,
      .progress_sink = [&bash_progress](ava::tools::ToolProgressEvent const& event) -> ava::core::VoidResult {
        bash_progress.push_back(event);
        return {};
      },
      .current_tool_name = "bash",
      .current_call_id = "call_progress"};
  auto progress_pwd = ava::tools::run_bash(progress_context, "pwd",
                                           ava::tools::BashOptions{.timeout = std::chrono::milliseconds(1000)});
  expect(progress_pwd && std::ranges::any_of(bash_progress,
                                             [](ava::tools::ToolProgressEvent const& event) {
                                               return event.call_id == "call_progress" && event.tool_name == "bash" &&
                                                      event.status == "completed";
                                             }),
         "run_bash emits bounded progress events with current call metadata");

  auto const bash_spill_dir = temp_root() / "session" / "bash-spill";
  ava::tools::ToolContext const bash_spill_context{
      .workspace_dir = temp_root(),
      .spill_dir = bash_spill_dir,
      .mode = ava::agent::Mode::Build,
      .permission_resolver = [](ava::permissions::PermissionPrompt const&)
          -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
        return ava::permissions::PermissionResolution::Allow;
      },
      .current_tool_name = "bash",
      .current_call_id = "call/bash:spill"};
  auto bash_spill =
      ava::tools::run_bash(bash_spill_context, "printf 0123456789abcdef",
                           ava::tools::BashOptions{.timeout = std::chrono::milliseconds(1000), .max_bytes = 4});
  expect(bash_spill && bash_spill->truncated && !bash_spill->spill_path.empty() &&
             bash_spill->spill_path.parent_path() == bash_spill_dir && std::filesystem::exists(bash_spill->spill_path),
         "run_bash spills truncated combined output under the configured spill directory");
  if (bash_spill && !bash_spill->spill_path.empty()) {
    expect(read_text_file_for_test(bash_spill->spill_path) == "0123456789abcdef",
           "bash spill file contains raw combined output from the beginning of the stream");
  }

  auto capped_spill =
      ava::tools::run_bash(bash_spill_context, "seq 1 1000000",
                           ava::tools::BashOptions{.timeout = std::chrono::milliseconds(5000), .max_bytes = 1});
  expect(capped_spill && capped_spill->truncated && capped_spill->spill_truncated &&
             !capped_spill->spill_path.empty() &&
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
    auto sanitized_pwd =
        ava::tools::run_bash(context, "pwd", ava::tools::BashOptions{.timeout = std::chrono::milliseconds(1000)});
    expect(sanitized_pwd && sanitized_pwd->exit_code == 0 &&
               sanitized_pwd->output.find("hijacked-path") == std::string::npos &&
               sanitized_pwd->output.find(temp_root().string()) != std::string::npos,
           "run_bash does not inherit unsafe PATH entries for auto-allowed commands");
  }

  auto denied = ava::tools::run_bash(context, "rm -rf important");
  expect(!denied, "run_bash denies destructive command");

  auto chained = ava::tools::run_bash(context, "pwd; rm -rf important");
  expect(!chained, "run_bash rejects shell metacharacters");
  auto allowed_prefix_chain = ava::tools::run_bash(context, "pwd; whoami");
  expect(!allowed_prefix_chain, "run_bash rejects shell metacharacters after allowed prefix");

  expect(!ava::tools::run_bash(context, "cmake -E cat ~/.config/ava/auth.json"),
         "run_bash denies cmake -E file helper before execution");
  expect(!ava::tools::run_bash(context, "cmake -P docs/plan.md"),
         "run_bash denies cmake script execution before execution");
  expect(!ava::tools::run_bash(context, "cmake -E copy docs/plan.md src/new.cpp"),
         "run_bash denies cmake copy helper before execution");

  auto timeout =
      ava::tools::run_bash(context, "sleep 2", ava::tools::BashOptions{.timeout = std::chrono::milliseconds(50)});
  expect(timeout && timeout->timed_out, "run_bash times out long command");

  auto const timeout_group_file = temp_root() / "bash-timeout-child-pgid.txt";
  std::filesystem::remove(timeout_group_file, remove_error);
  int timeout_tree_prompts = 0;
  ava::tools::ToolContext const timeout_tree_context{
      .workspace_dir = temp_root(),
      .mode = ava::agent::Mode::Build,
      .permission_resolver = [&timeout_tree_prompts](ava::permissions::PermissionPrompt const& prompt)
          -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
        ++timeout_tree_prompts;
        expect(prompt.operation == ava::permissions::Operation::RunCommand,
               "bash process-tree timeout resolver receives run operation");
        return ava::permissions::PermissionResolution::Allow;
      }};
  auto timeout_tree = ava::tools::run_bash(
      timeout_tree_context,
      "/bin/sh -c \"sleep 30 & printf $$ > " + timeout_group_file.generic_string() + "; wait\"",
      ava::tools::BashOptions{.timeout = std::chrono::milliseconds(500), .max_bytes = 1024});
  auto const timeout_pgid = read_pid_file_for_test(timeout_group_file);
  expect(timeout_tree && timeout_tree->timed_out && timeout_tree_prompts == 1 && timeout_pgid &&
             wait_for_process_group_exit(*timeout_pgid),
         "run_bash timeout terminates child processes in the command process group");

  int cancel_checks = 0;
  ava::tools::ToolContext const cancel_context{
      .workspace_dir = temp_root(), .mode = ava::agent::Mode::Build, .cancel_requested = [&cancel_checks] {
        ++cancel_checks;
        return cancel_checks >= 3;
      }};
  auto canceled = ava::tools::run_bash(cancel_context, "sleep 2",
                                       ava::tools::BashOptions{.timeout = std::chrono::milliseconds(5000)});
  expect(canceled && canceled->canceled && !canceled->timed_out && canceled->exit_code == -1,
         "run_bash observes tool cancellation and reports a canceled process result");

  auto const cancel_group_file = temp_root() / "bash-cancel-child-pgid.txt";
  std::filesystem::remove(cancel_group_file, remove_error);
  int cancel_tree_prompts = 0;
  ava::tools::ToolContext const cancel_tree_context{
      .workspace_dir = temp_root(),
      .mode = ava::agent::Mode::Build,
      .permission_resolver = [&cancel_tree_prompts](ava::permissions::PermissionPrompt const& prompt)
          -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
        ++cancel_tree_prompts;
        expect(prompt.operation == ava::permissions::Operation::RunCommand,
               "bash process-tree cancel resolver receives run operation");
        return ava::permissions::PermissionResolution::Allow;
      },
      .cancel_requested = [&cancel_group_file] { return std::filesystem::exists(cancel_group_file); }};
  auto cancel_tree = ava::tools::run_bash(
      cancel_tree_context,
      "/bin/sh -c \"sleep 30 & printf $$ > " + cancel_group_file.generic_string() + "; wait\"",
      ava::tools::BashOptions{.timeout = std::chrono::milliseconds(5000), .max_bytes = 1024});
  auto const cancel_pgid = read_pid_file_for_test(cancel_group_file);
  expect(cancel_tree && cancel_tree->canceled && !cancel_tree->timed_out && cancel_tree_prompts == 1 && cancel_pgid &&
             wait_for_process_group_exit(*cancel_pgid),
         "run_bash cancellation terminates child processes in the command process group");

  auto ask_without_resolver = ava::tools::run_bash(context, "true");
  expect(!ask_without_resolver &&
             ask_without_resolver.error().format().find("resolution: no_resolver") != std::string::npos,
         "run_bash fails closed for ask decisions without a resolver");

  int bash_prompts = 0;
  ava::tools::ToolContext allow_context{
      .workspace_dir = temp_root(),
      .mode = ava::agent::Mode::Build,
      .permission_resolver = [&bash_prompts](ava::permissions::PermissionPrompt const& prompt)
          -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
        ++bash_prompts;
        expect(prompt.operation == ava::permissions::Operation::RunCommand, "bash resolver receives run operation");
        expect(prompt.command == "true", "bash resolver receives command text");
        return ava::permissions::PermissionResolution::Allow;
      }};
  auto ask_allowed = ava::tools::run_bash(allow_context, "true");
  expect(ask_allowed && ask_allowed->exit_code == 0 && bash_prompts == 1,
         "run_bash allows ask decisions when resolver allows once");

  ava::tools::ToolContext deny_context{.workspace_dir = temp_root(),
                                       .mode = ava::agent::Mode::Build,
                                       .permission_resolver = [](ava::permissions::PermissionPrompt const&)
                                           -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
                                         return ava::permissions::PermissionResolution::Deny;
                                       }};
  auto ask_denied = ava::tools::run_bash(deny_context, "true");
  expect(!ask_denied && ask_denied.error().format().find("resolution: deny") != std::string::npos,
         "run_bash fails closed when resolver denies ask decisions");

  ava::tools::ToolContext failing_context{
      .workspace_dir = temp_root(),
      .mode = ava::agent::Mode::Build,
      .permission_resolver = [](ava::permissions::PermissionPrompt const&)
          -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
        return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "resolver failed"));
      }};
  auto ask_failed = ava::tools::run_bash(failing_context, "true");
  expect(!ask_failed && ask_failed.error().format().find("resolution: resolver_failed") != std::string::npos,
         "run_bash fails closed when resolver fails");
}

void test_webfetch_tool()
{
  std::error_code remove_error;
  std::filesystem::remove_all(temp_root(), remove_error);
  std::filesystem::create_directories(temp_root());
  auto const workspace = temp_root() / "webfetch-workspace";
  std::filesystem::create_directories(workspace);

  StaticTransport transport(ava::provider::HttpResponse{
      .status_code = 200, .headers = {{"content-type", "text/plain; charset=utf-8"}}, .body = "abcdef"});
  int prompts = 0;
  ava::tools::ToolContext const context{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .permission_resolver = [&prompts](ava::permissions::PermissionPrompt const& prompt)
          -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
        ++prompts;
        expect(prompt.operation == ava::permissions::Operation::NetworkFetch,
               "webfetch resolver receives network operation");
        expect(prompt.command == "https://example.com/page", "webfetch resolver receives URL as command target");
        return ava::permissions::PermissionResolution::Allow;
      }};
  auto fetched =
      ava::tools::webfetch(context, "https://example.com/page",
                           ava::tools::WebFetchOptions{.max_bytes = 3, .timeout_ms = 5000, .transport = &transport});
  expect(fetched && fetched->content == "abc" && fetched->truncated && fetched->byte_limited &&
             fetched->total_bytes == 6 && fetched->output_bytes == 3 && fetched->output_lines == 1 &&
             fetched->content_type == "text/plain; charset=utf-8" && prompts == 1 && transport.requests.size() == 1 &&
             transport.requests[0].method == "GET" && transport.requests[0].timeout_ms == 5000 &&
             !transport.requests[0].follow_redirects && transport.requests[0].include_response_headers,
         "webfetch requires permission and bounds fetched text content");

  StaticTransport multiline_transport(
      ava::provider::HttpResponse{.status_code = 200,
                                  .headers = {{"content-type", "text/plain; charset=utf-8"}},
                                  .body = "one\ntwo\nthree\nfour\n"});
  auto fetched_lines = ava::tools::webfetch(
      context, "https://example.com/page",
      ava::tools::WebFetchOptions{
          .max_bytes = 1024, .offset_line = 2, .max_lines = 2, .timeout_ms = 5000, .transport = &multiline_transport});
  expect(fetched_lines && fetched_lines->content == "two\nthree\n" && fetched_lines->line_limited &&
             !fetched_lines->byte_limited && fetched_lines->output_lines == 2 && fetched_lines->start_line == 2 &&
             fetched_lines->end_line == 3 && fetched_lines->total_lines == 4 && fetched_lines->next_offset_line == 4,
         "webfetch supports line offset and limit continuation metadata");

  StaticTransport html_transport(ava::provider::HttpResponse{
      .status_code = 200,
      .headers = {{"content-type", "text/html; charset=utf-8"}},
      .body = "<html><body><h1>Title</h1><script>hidden()</script><p>A&amp;B</p></body></html>"});
  auto html_text = ava::tools::webfetch(context, "https://example.com/page",
                                        ava::tools::WebFetchOptions{.max_bytes = 1024,
                                                                    .timeout_ms = 5000,
                                                                    .format = ava::tools::WebFetchFormat::Text,
                                                                    .transport = &html_transport});
  expect(html_text && html_text->content.find("Title") != std::string::npos &&
             html_text->content.find("A&B") != std::string::npos &&
             html_text->content.find("hidden") == std::string::npos &&
             html_transport.requests[0].headers.at("Accept").find("text/plain") != std::string::npos,
         "webfetch supports text output for HTML responses with basic tag stripping");

  StaticTransport unused_transport(ava::provider::HttpResponse{.status_code = 200, .headers = {}, .body = "unused"});
  auto invalid_scheme =
      ava::tools::webfetch(context, "file:///etc/passwd", ava::tools::WebFetchOptions{.transport = &unused_transport});
  expect(!invalid_scheme && unused_transport.requests.empty(), "webfetch rejects non-http URLs before transport use");

  auto private_ip = ava::tools::webfetch(context, "http://127.0.0.1:8080",
                                         ava::tools::WebFetchOptions{.transport = &unused_transport});
  expect(!private_ip && unused_transport.requests.empty(), "webfetch rejects local IP hosts before transport use");

  auto short_ipv4 =
      ava::tools::webfetch(context, "http://127.1:8080", ava::tools::WebFetchOptions{.transport = &unused_transport});
  expect(!short_ipv4 && unused_transport.requests.empty(), "webfetch rejects shortened IPv4 literal hosts");

  auto decimal_ipv4 =
      ava::tools::webfetch(context, "http://2130706433/", ava::tools::WebFetchOptions{.transport = &unused_transport});
  expect(!decimal_ipv4 && unused_transport.requests.empty(), "webfetch rejects decimal IPv4 literal hosts");

  auto hex_ipv4 =
      ava::tools::webfetch(context, "http://0x7f000001/", ava::tools::WebFetchOptions{.transport = &unused_transport});
  expect(!hex_ipv4 && unused_transport.requests.empty(), "webfetch rejects hexadecimal IPv4 literal hosts");

  StaticTransport digit_domain_transport(
      ava::provider::HttpResponse{.status_code = 200, .headers = {}, .body = "domain ok"});
  ava::tools::ToolContext const permissive_network_context{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .permission_resolver = [](ava::permissions::PermissionPrompt const& prompt)
          -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
        expect(prompt.operation == ava::permissions::Operation::NetworkFetch,
               "digit-leading domain still requests network permission");
        return ava::permissions::PermissionResolution::Allow;
      }};
  auto digit_domain = ava::tools::webfetch(permissive_network_context, "https://1.be/",
                                           ava::tools::WebFetchOptions{.transport = &digit_domain_transport});
  expect(digit_domain && digit_domain->content == "domain ok",
         "webfetch allows digit-leading DNS names that are not IP aliases");

  ava::tools::ToolContext const no_resolver_context{.workspace_dir = workspace, .mode = ava::agent::Mode::Build};
  auto no_resolver = ava::tools::webfetch(no_resolver_context, "https://example.com/page",
                                          ava::tools::WebFetchOptions{.transport = &unused_transport});
  expect(!no_resolver && no_resolver.error().format().find("no_resolver") != std::string::npos &&
             unused_transport.requests.empty(),
         "webfetch fails closed without network permission resolver");

  StaticTransport binary_transport(ava::provider::HttpResponse{
      .status_code = 200, .headers = {{"content-type", "application/octet-stream"}}, .body = "abc"});
  auto binary = ava::tools::webfetch(context, "https://example.com/page",
                                     ava::tools::WebFetchOptions{.transport = &binary_transport});
  expect(!binary && binary.error().message().find("binary") != std::string::npos,
         "webfetch rejects binary response content types");

  CancelAwareTransport cancel_aware_transport;
  int cancel_checks = 0;
  ava::tools::ToolContext const cancel_during_transport_context{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .permission_resolver = [](ava::permissions::PermissionPrompt const&)
          -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
        return ava::permissions::PermissionResolution::Allow;
      },
      .cancel_requested =
          [&cancel_checks] {
            ++cancel_checks;
            return cancel_checks >= 3;
          },
  };
  auto canceled_fetch = ava::tools::webfetch(cancel_during_transport_context, "https://example.com/page",
                                             ava::tools::WebFetchOptions{.transport = &cancel_aware_transport});
  expect(!canceled_fetch && canceled_fetch.error().message().find("canceled") != std::string::npos &&
             cancel_aware_transport.saw_cancel_callback && cancel_aware_transport.requests.size() == 1,
         "webfetch passes cancellation into the transport request boundary");
}

void test_websearch_tool()
{
  std::error_code remove_error;
  std::filesystem::remove_all(temp_root(), remove_error);
  auto const workspace = temp_root() / "websearch-workspace";
  std::filesystem::create_directories(workspace);

  StaticTransport transport(ava::provider::HttpResponse{
      .status_code = 200,
      .headers = {{"content-type", "application/json"}},
      .body = "{\"Heading\":\"AVA\",\"AbstractText\":\"Native C++ agent\",\"AbstractURL\":\"https://ava.example/\","
              "\"RelatedTopics\":[{\"Text\":\"AVA docs\",\"FirstURL\":\"https://ava.example/docs\"},"
              "{\"Topics\":[{\"Text\":\"AVA releases\",\"FirstURL\":\"https://ava.example/releases\"}]}]}"});
  int prompts = 0;
  ava::tools::ToolContext const context{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .permission_resolver = [&prompts](ava::permissions::PermissionPrompt const& prompt)
          -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
        ++prompts;
        expect(prompt.operation == ava::permissions::Operation::NetworkSearch,
               "websearch resolver receives network search operation");
        expect(prompt.command == "ava agent", "websearch resolver receives query as command target");
        return ava::permissions::PermissionResolution::Allow;
      }};

  auto searched = ava::tools::websearch(
      context, " ava agent ",
      ava::tools::WebSearchOptions{
          .max_results = 2, .context_max_chars = 10000, .timeout_ms = 7000, .transport = &transport});
  expect(searched && searched->query == "ava agent" && searched->results.size() == 2 &&
             searched->results[0].title == "AVA" && searched->results[1].url == "https://ava.example/docs" &&
             searched->total_results == 3 && searched->truncated && prompts == 1 && transport.requests.size() == 1 &&
             transport.requests[0].method == "GET" &&
             transport.requests[0].url.find("q=ava+agent") != std::string::npos &&
             transport.requests[0].timeout_ms == 7000 && transport.requests[0].follow_redirects,
         "websearch requires permission, queries a bounded search endpoint, and parses structured results");

  StaticTransport unused_transport(ava::provider::HttpResponse{.status_code = 200, .headers = {}, .body = "{}"});
  auto invalid_query =
      ava::tools::websearch(context, "\n", ava::tools::WebSearchOptions{.transport = &unused_transport});
  expect(!invalid_query && unused_transport.requests.empty(),
         "websearch rejects empty/control queries before transport use");

  ava::tools::ToolContext const no_resolver_context{.workspace_dir = workspace, .mode = ava::agent::Mode::Build};
  auto no_resolver = ava::tools::websearch(no_resolver_context, "ava agent",
                                           ava::tools::WebSearchOptions{.transport = &unused_transport});
  expect(!no_resolver && no_resolver.error().format().find("no_resolver") != std::string::npos &&
             unused_transport.requests.empty(),
         "websearch fails closed without network search permission resolver");
}
}  // namespace

void run_tools_process_network_tests()
{
  test_bash_tool();
  test_webfetch_tool();
  test_websearch_tool();
}
