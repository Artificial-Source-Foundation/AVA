#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <climits>
#include <cstdlib>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "ava/agent/agent_loop.h"
#include "ava/agent/mode.h"
#include "ava/agent/tool_dispatcher.h"
#include "ava/app/commands.h"
#include "ava/app/events.h"
#include "ava/app/headless_policy.h"
#include "ava/app/print_mode.h"
#include "ava/app/rpc_mode.h"
#include "ava/app/runtime.h"
#include "ava/config/auth.h"
#include "ava/config/model_config.h"
#include "ava/config/openai_oauth.h"
#include "ava/config/prompt_config.h"
#include "ava/config/xdg_paths.h"
#include "ava/context/context_loader.h"
#include "ava/core/ids.h"
#include "ava/core/json.h"
#include "ava/permissions/permission.h"
#include "ava/provider/openai_provider.h"
#include "ava/session/compaction.h"
#include "ava/session/export.h"
#include "ava/session/session_store.h"
#include "ava/tools/bash_tool.h"
#include "ava/tools/file_tools.h"
#include "ava/tools/search_tools.h"
#include "ava/tui/composer.h"
#include "ava/tui/terminal.h"
#include "tests/support/fake_transport.h"

#include "tests/support/test_harness.h"

namespace {

ava::provider::HttpResponse sse_response(const std::string& body) {
  return ava::provider::HttpResponse{.status_code = 200, .headers = {}, .body = body};
}

class CallbackTransport final : public ava::provider::Transport {
 public:
  CallbackTransport(std::vector<ava::provider::HttpResponse> responses, std::function<void()> after_send)
      : responses_(std::move(responses)), after_send_(std::move(after_send)) {}

  [[nodiscard]] ava::core::Result<ava::provider::HttpResponse> send(
      const ava::provider::HttpRequest& request) override {
    requests_.push_back(request);
    if (responses_.empty()) {
      return std::unexpected(
          ava::core::Error(ava::core::ErrorCategory::Provider, "callback transport has no response"));
    }
    auto response = responses_.front();
    responses_.erase(responses_.begin());
    if (after_send_) after_send_();
    return response;
  }

  [[nodiscard]] const std::vector<ava::provider::HttpRequest>& requests() const noexcept { return requests_; }

 private:
  std::vector<ava::provider::HttpResponse> responses_;
  std::function<void()> after_send_;
  std::vector<ava::provider::HttpRequest> requests_;
};

void test_tool_dispatcher() {
  const auto root = temp_root() / "dispatcher";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  const auto permission_bits = [](const std::filesystem::path& permission_path) {
    constexpr auto mask =
        std::filesystem::perms::owner_all | std::filesystem::perms::group_all | std::filesystem::perms::others_all;
    std::error_code status_error;
    return std::filesystem::status(permission_path, status_error).permissions() & mask;
  };
  {
    std::ofstream file(workspace / "note.txt", std::ios::binary | std::ios::trunc);
    file << "hello dispatcher";
  }

  const ava::agent::ToolDispatcher dispatcher(
      ava::tools::ToolContext{.workspace_dir = workspace, .mode = ava::agent::Mode::Build});
  auto read = dispatcher.dispatch(ava::agent::ProviderToolCall{
      .id = "call_read", .name = "read_file", .arguments_json = "{\"path\":\"note.txt\",\"max_bytes\":5}"});
  expect(read && read->success && read->result_text.find("hello") != std::string::npos,
         "tool dispatcher maps read_file provider call to file tool");

  auto control_call_id = dispatcher.dispatch(ava::agent::ProviderToolCall{
      .id = std::string("call_") + '\x01' + "bad", .name = "read_file", .arguments_json = "{\"path\":\"note.txt\"}"});
  expect(!control_call_id && control_call_id.error().message().find("control byte") != std::string::npos,
         "tool dispatcher rejects provider call ids with control bytes before tool use");

  auto long_call_id = dispatcher.dispatch(ava::agent::ProviderToolCall{
      .id = std::string(300, 'a'), .name = "read_file", .arguments_json = "{\"path\":\"note.txt\"}"});
  expect(!long_call_id && long_call_id.error().message().find("too long") != std::string::npos,
         "tool dispatcher rejects overlong provider call ids before tool use");

  auto nul_path = dispatcher.dispatch(ava::agent::ProviderToolCall{
      .id = "call_nul_path", .name = "read_file", .arguments_json = "{\"path\":\"note\\u0000.txt\"}"});
  expect(nul_path && !nul_path->success && nul_path->result_text.find("control byte") != std::string::npos,
         "tool dispatcher rejects NUL bytes decoded into file paths");

  auto nul_command = dispatcher.dispatch(ava::agent::ProviderToolCall{
      .id = "call_nul_command", .name = "bash", .arguments_json = "{\"command\":\"pwd\\u0000whoami\"}"});
  expect(nul_command && !nul_command->success && nul_command->result_text.find("control byte") != std::string::npos,
         "tool dispatcher rejects NUL bytes decoded into commands");

  auto nul_content = dispatcher.dispatch(
      ava::agent::ProviderToolCall{.id = "call_nul_content",
                                   .name = "write_file",
                                   .arguments_json = "{\"path\":\"nul.txt\",\"content\":\"bad\\u0000text\"}"});
  expect(nul_content && !nul_content->success && nul_content->result_text.find("NUL byte") != std::string::npos &&
             !std::filesystem::exists(workspace / "nul.txt"),
         "tool dispatcher rejects NUL bytes in text arguments before writing");

  auto nul_include = dispatcher.dispatch(
      ava::agent::ProviderToolCall{.id = "call_nul_include",
                                   .name = "grep",
                                   .arguments_json = "{\"pattern\":\"hello\",\"include\":\"**/*\\u0000\"}"});
  expect(nul_include && !nul_include->success && nul_include->result_text.find("control byte") != std::string::npos,
         "tool dispatcher rejects NUL bytes decoded into grep include globs");

  auto malformed_args = dispatcher.dispatch(
      ava::agent::ProviderToolCall{.id = "call_bad_args", .name = "read_file", .arguments_json = "{not-json}"});
  expect(
      malformed_args && !malformed_args->success && malformed_args->result_text.find("required") != std::string::npos,
      "tool dispatcher returns structured errors for malformed tool arguments");

  auto patch = dispatcher.dispatch(ava::agent::ProviderToolCall{
      .id = "call_patch",
      .name = "apply_patch",
      .arguments_json = "{\"edits\":[{\"path\":\"note.txt\",\"old_text\":\"dispatcher\",\"new_text\":\"patch\"}]}"});
  expect(patch && patch->success && patch->result_text.find("apply_patch") != std::string::npos,
         "tool dispatcher applies exact patch edits");
  auto patched_read = dispatcher.dispatch(ava::agent::ProviderToolCall{
      .id = "call_patched_read", .name = "read_file", .arguments_json = "{\"path\":\"note.txt\"}"});
  expect(patched_read && patched_read->result_text.find("hello patch") != std::string::npos,
         "apply_patch updates file content through file tools");

  const auto private_patch_path = workspace / "private-patch.txt";
  {
    std::ofstream file(private_patch_path, std::ios::binary | std::ios::trunc);
    file << "private old";
  }
  std::error_code chmod_error;
  std::filesystem::permissions(private_patch_path,
                               std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                               std::filesystem::perm_options::replace, chmod_error);
  expect(!chmod_error, "test can set private patch file permissions");
  auto private_patch = dispatcher.dispatch(ava::agent::ProviderToolCall{
      .id = "call_private_patch",
      .name = "apply_patch",
      .arguments_json = "{\"edits\":[{\"path\":\"private-patch.txt\",\"old_text\":\"old\",\"new_text\":\"new\"}]}"});
  auto private_patch_read = ava::tools::read_file(
      ava::tools::ToolContext{.workspace_dir = workspace, .mode = ava::agent::Mode::Build}, private_patch_path);
  expect(private_patch && private_patch->success && private_patch_read && private_patch_read->content == "private new" &&
             permission_bits(private_patch_path) ==
                 (std::filesystem::perms::owner_read | std::filesystem::perms::owner_write),
         "apply_patch preserves 0600 permissions when replacing an existing file");

  const auto audit_patch_path = workspace / "audit-patch.txt";
  {
    std::ofstream file(audit_patch_path, std::ios::binary | std::ios::trunc);
    file << "audit old";
  }
  std::vector<ava::tools::PermissionAuditEvent> patch_audits;
  const ava::agent::ToolDispatcher audit_patch_dispatcher(ava::tools::ToolContext{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .permission_audit_sink = [&patch_audits](const ava::tools::PermissionAuditEvent& event) -> ava::core::VoidResult {
        patch_audits.push_back(event);
        return {};
      }});
  auto audited_patch = audit_patch_dispatcher.dispatch(ava::agent::ProviderToolCall{
      .id = "call_audit_patch",
      .name = "apply_patch",
      .arguments_json = "{\"edits\":[{\"path\":\"audit-patch.txt\",\"old_text\":\"old\",\"new_text\":\"new\"}]}"});
  expect(audited_patch && audited_patch->success && patch_audits.size() == 2 &&
             patch_audits[0].operation == ava::permissions::Operation::ReadFile &&
             patch_audits[0].tool_name == "apply_patch" &&
             patch_audits[1].operation == ava::permissions::Operation::EditFile &&
             patch_audits[1].tool_name == "apply_patch",
         "apply_patch audits read permission before edit permission");

  {
    std::ofstream file(workspace / "sequential.txt", std::ios::binary | std::ios::trunc);
    file << "one two";
  }
  auto sequential_patch = dispatcher.dispatch(ava::agent::ProviderToolCall{
      .id = "call_sequential_patch",
      .name = "apply_patch",
      .arguments_json = "{\"edits\":[{\"path\":\"sequential.txt\",\"old_text\":\"one\",\"new_text\":\"two\"},"
                        "{\"path\":\"sequential.txt\",\"old_text\":\"two\",\"new_text\":\"three\"}]}"});
  auto sequential_read =
      ava::tools::read_file(ava::tools::ToolContext{.workspace_dir = workspace, .mode = ava::agent::Mode::Build},
                            workspace / "sequential.txt");
  expect(sequential_patch && sequential_patch->success && sequential_read && sequential_read->content == "two three",
         "apply_patch validates same-file edits against original content before applying replacements");

  {
    std::ofstream file(workspace / "overlap.txt", std::ios::binary | std::ios::trunc);
    file << "abcde";
  }
  auto overlap_patch = dispatcher.dispatch(ava::agent::ProviderToolCall{
      .id = "call_overlap_patch",
      .name = "apply_patch",
      .arguments_json = "{\"edits\":[{\"path\":\"overlap.txt\",\"old_text\":\"abc\",\"new_text\":\"x\"},"
                        "{\"path\":\"overlap.txt\",\"old_text\":\"cde\",\"new_text\":\"y\"}]}"});
  auto overlap_read = ava::tools::read_file(
      ava::tools::ToolContext{.workspace_dir = workspace, .mode = ava::agent::Mode::Build}, workspace / "overlap.txt");
  expect(overlap_patch && !overlap_patch->success && overlap_read && overlap_read->content == "abcde" &&
             overlap_patch->result_text.find("patch edits overlap") != std::string::npos,
         "apply_patch rejects overlapping same-file edits before writing");

  auto empty_old_patch = dispatcher.dispatch(ava::agent::ProviderToolCall{
      .id = "call_empty_old_patch",
      .name = "apply_patch",
      .arguments_json = "{\"edits\":[{\"path\":\"note.txt\",\"old_text\":\"\",\"new_text\":\"bad\"}]}"});
  expect(empty_old_patch && !empty_old_patch->success &&
             empty_old_patch->result_text.find("old_text must not be empty") != std::string::npos,
         "apply_patch rejects empty old_text before attempting a match");

  const auto outside_path = root / "outside.txt";
  {
    std::ofstream outside_file(outside_path, std::ios::binary | std::ios::trunc);
    outside_file << "dispatcher outside";
  }
  int dispatcher_prompts = 0;
  const ava::agent::ToolDispatcher resolving_dispatcher(ava::tools::ToolContext{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .permission_resolver = [&dispatcher_prompts](const ava::permissions::PermissionPrompt& prompt)
          -> ava::core::Result<ava::permissions::PermissionResolution> {
        ++dispatcher_prompts;
        expect(prompt.tool_name == "read_file", "dispatcher threads provider tool prompt metadata");
        return ava::permissions::PermissionResolution::Allow;
      }});
  auto outside_read = resolving_dispatcher.dispatch(ava::agent::ProviderToolCall{
      .id = "call_outside_read",
      .name = "read_file",
      .arguments_json = "{\"path\":\"" + ava::core::json::escape(outside_path.generic_string()) + "\"}"});
  expect(outside_read && outside_read->success &&
             outside_read->result_text.find("dispatcher outside") != std::string::npos && dispatcher_prompts == 1,
         "tool dispatcher threads resolver into file tools");

  const auto outside_patch_path = root / "outside-patch.txt";
  {
    std::ofstream outside_patch_file(outside_patch_path, std::ios::binary | std::ios::trunc);
    outside_patch_file << "outside old";
  }
  std::vector<ava::permissions::Operation> apply_patch_prompts;
  const ava::agent::ToolDispatcher patch_resolving_dispatcher(ava::tools::ToolContext{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .permission_resolver = [&apply_patch_prompts](const ava::permissions::PermissionPrompt& prompt)
          -> ava::core::Result<ava::permissions::PermissionResolution> {
        apply_patch_prompts.push_back(prompt.operation);
        expect(prompt.tool_name == "apply_patch", "apply_patch resolver receives tool name");
        return ava::permissions::PermissionResolution::Allow;
      }});
  auto outside_patch = patch_resolving_dispatcher.dispatch(ava::agent::ProviderToolCall{
      .id = "call_outside_patch",
      .name = "apply_patch",
      .arguments_json = "{\"edits\":[{\"path\":\"" + ava::core::json::escape(outside_patch_path.generic_string()) +
                        "\",\"old_text\":\"old\",\"new_text\":\"new\"},{\"path\":\"" +
                        ava::core::json::escape(outside_patch_path.generic_string()) +
                        "\",\"old_text\":\"outside\",\"new_text\":\"inside\"}]}"});
  auto outside_patch_read = ava::tools::read_file(
      ava::tools::ToolContext{.workspace_dir = root, .mode = ava::agent::Mode::Build}, outside_patch_path);
  expect(outside_patch && outside_patch->success && outside_patch_read && outside_patch_read->content == "inside new" &&
             apply_patch_prompts.size() == 2 && apply_patch_prompts[0] == ava::permissions::Operation::ReadFile &&
             apply_patch_prompts[1] == ava::permissions::Operation::EditFile,
         "apply_patch resolves external read permission before edit permission");

  const auto outside_no_resolver_path = root / "outside-patch-no-resolver.txt";
  {
    std::ofstream file(outside_no_resolver_path, std::ios::binary | std::ios::trunc);
    file << "keep old";
  }
  auto outside_patch_no_resolver = dispatcher.dispatch(ava::agent::ProviderToolCall{
      .id = "call_outside_patch_no_resolver",
      .name = "apply_patch",
      .arguments_json = "{\"edits\":[{\"path\":\"" +
                        ava::core::json::escape(outside_no_resolver_path.generic_string()) +
                        "\",\"old_text\":\"old\",\"new_text\":\"new\"}]}"});
  auto outside_no_resolver_read = ava::tools::read_file(
      ava::tools::ToolContext{.workspace_dir = root, .mode = ava::agent::Mode::Build}, outside_no_resolver_path);
  expect(outside_patch_no_resolver && !outside_patch_no_resolver->success && outside_no_resolver_read &&
             outside_no_resolver_read->content == "keep old" &&
             outside_patch_no_resolver->result_text.find("no_resolver") != std::string::npos,
         "apply_patch fails closed without resolver and does not write external targets");

  const auto outside_denied_patch_path = root / "outside-patch-denied.txt";
  {
    std::ofstream file(outside_denied_patch_path, std::ios::binary | std::ios::trunc);
    file << "keep old";
  }
  int denied_patch_prompts = 0;
  const ava::agent::ToolDispatcher patch_denying_dispatcher(
      ava::tools::ToolContext{.workspace_dir = workspace,
                              .mode = ava::agent::Mode::Build,
                              .permission_resolver = [&denied_patch_prompts](
                                                         const ava::permissions::PermissionPrompt& prompt)
                                  -> ava::core::Result<ava::permissions::PermissionResolution> {
                                ++denied_patch_prompts;
                                expect(prompt.operation == ava::permissions::Operation::ReadFile,
                                       "apply_patch resolver sees read operation before denied external patch");
                                return ava::permissions::PermissionResolution::Deny;
                              }});
  auto outside_patch_denied = patch_denying_dispatcher.dispatch(ava::agent::ProviderToolCall{
      .id = "call_outside_patch_denied",
      .name = "apply_patch",
      .arguments_json = "{\"edits\":[{\"path\":\"" +
                        ava::core::json::escape(outside_denied_patch_path.generic_string()) +
                        "\",\"old_text\":\"old\",\"new_text\":\"new\"}]}"});
  auto outside_denied_patch_read = ava::tools::read_file(
      ava::tools::ToolContext{.workspace_dir = root, .mode = ava::agent::Mode::Build}, outside_denied_patch_path);
  expect(outside_patch_denied && !outside_patch_denied->success && denied_patch_prompts == 1 &&
             outside_denied_patch_read && outside_denied_patch_read->content == "keep old" &&
             outside_patch_denied->result_text.find("resolution: deny") != std::string::npos,
         "apply_patch resolver read denial prevents all external writes");

  const auto outside_failed_patch_path = root / "outside-patch-failed.txt";
  {
    std::ofstream file(outside_failed_patch_path, std::ios::binary | std::ios::trunc);
    file << "keep old";
  }
  const ava::agent::ToolDispatcher patch_failing_dispatcher(ava::tools::ToolContext{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .permission_resolver =
          [](const ava::permissions::PermissionPrompt&) -> ava::core::Result<ava::permissions::PermissionResolution> {
        return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "resolver failed"));
      }});
  auto outside_patch_failed = patch_failing_dispatcher.dispatch(ava::agent::ProviderToolCall{
      .id = "call_outside_patch_failed",
      .name = "apply_patch",
      .arguments_json = "{\"edits\":[{\"path\":\"" +
                        ava::core::json::escape(outside_failed_patch_path.generic_string()) +
                        "\",\"old_text\":\"old\",\"new_text\":\"new\"}]}"});
  auto outside_failed_patch_read = ava::tools::read_file(
      ava::tools::ToolContext{.workspace_dir = root, .mode = ava::agent::Mode::Build}, outside_failed_patch_path);
  expect(outside_patch_failed && !outside_patch_failed->success && outside_failed_patch_read &&
             outside_failed_patch_read->content == "keep old" &&
             outside_patch_failed->result_text.find("resolver_failed") != std::string::npos,
         "apply_patch resolver failure prevents all external writes");

  const auto partial_a = workspace / "partial-a.txt";
  const auto partial_b = workspace / "partial-b.txt";
  {
    std::ofstream a(partial_a, std::ios::binary | std::ios::trunc);
    a << "alpha old";
    std::ofstream b(partial_b, std::ios::binary | std::ios::trunc);
    b << "beta stays";
  }
  auto partial_patch = dispatcher.dispatch(ava::agent::ProviderToolCall{
      .id = "call_partial_patch",
      .name = "apply_patch",
      .arguments_json = "{\"edits\":[{\"path\":\"partial-a.txt\",\"old_text\":\"old\",\"new_text\":\"new\"},"
                        "{\"path\":\"partial-b.txt\",\"old_text\":\"missing\",\"new_text\":\"new\"}]}"});
  auto partial_a_read = ava::tools::read_file(
      ava::tools::ToolContext{.workspace_dir = workspace, .mode = ava::agent::Mode::Build}, partial_a);
  auto partial_b_read = ava::tools::read_file(
      ava::tools::ToolContext{.workspace_dir = workspace, .mode = ava::agent::Mode::Build}, partial_b);
  expect(partial_patch && !partial_patch->success && partial_a_read && partial_b_read &&
             partial_a_read->content == "alpha old" && partial_b_read->content == "beta stays",
         "apply_patch validates all edits before writing so failures do not partially write");

  const auto staged_a = workspace / "staged-a.txt";
  {
    std::ofstream a(staged_a, std::ios::binary | std::ios::trunc);
    a << "stage alpha old";
  }
  const long name_max = pathconf(workspace.c_str(), _PC_NAME_MAX);
  if (name_max > 64 && name_max < 10000) {
    const std::string long_patch_name(static_cast<std::size_t>(name_max) - 4, 'l');
    const auto staged_long = workspace / (long_patch_name + ".txt");
    auto long_setup =
        ava::tools::write_file(ava::tools::ToolContext{.workspace_dir = workspace, .mode = ava::agent::Mode::Build},
                               staged_long, "stage beta old");
    if (long_setup) {
      auto staged_patch = dispatcher.dispatch(ava::agent::ProviderToolCall{
          .id = "call_staged_patch_failure",
          .name = "apply_patch",
          .arguments_json = "{\"edits\":[{\"path\":\"staged-a.txt\",\"old_text\":\"old\",\"new_text\":\"new\"},"
                            "{\"path\":\"" +
                            ava::core::json::escape(staged_long.filename().generic_string()) +
                            "\",\"old_text\":\"old\",\"new_text\":\"new\"}]}"});
      auto staged_a_read = ava::tools::read_file(
          ava::tools::ToolContext{.workspace_dir = workspace, .mode = ava::agent::Mode::Build}, staged_a);
      auto staged_long_read = ava::tools::read_file(
          ava::tools::ToolContext{.workspace_dir = workspace, .mode = ava::agent::Mode::Build}, staged_long);
      bool has_leftover_stage_temp = false;
      std::error_code iter_error;
      for (std::filesystem::directory_iterator it(workspace, iter_error), end; !iter_error && it != end;
           it.increment(iter_error)) {
        has_leftover_stage_temp =
            has_leftover_stage_temp || it->path().filename().string().find(".ava-patch-") != std::string::npos;
      }
      expect(
          staged_patch && !staged_patch->success && staged_a_read && staged_long_read &&
              staged_a_read->content == "stage alpha old" && staged_long_read->content == "stage beta old" &&
              !has_leftover_stage_temp && staged_patch->result_text.find("temporary_patch_write") != std::string::npos,
          "apply_patch stages all writes before commit and leaves originals unchanged when staging later files fails");
    }
  }

  const auto too_large_patch_path = workspace / "too-large-patch.txt";
  {
    std::ofstream large_patch_file(too_large_patch_path, std::ios::binary | std::ios::trunc);
    large_patch_file << "old" << std::string((10 * 1024 * 1024) + 1, 'x');
  }
  auto too_large_patch = dispatcher.dispatch(ava::agent::ProviderToolCall{
      .id = "call_too_large_patch",
      .name = "apply_patch",
      .arguments_json = "{\"edits\":[{\"path\":\"too-large-patch.txt\",\"old_text\":\"old\",\"new_text\":\"new\"}]}"});
  std::ifstream too_large_patch_read(too_large_patch_path, std::ios::binary);
  std::string too_large_prefix(3, '\0');
  too_large_patch_read.read(too_large_prefix.data(), static_cast<std::streamsize>(too_large_prefix.size()));
  expect(too_large_patch && !too_large_patch->success &&
             too_large_patch->result_text.find("too large") != std::string::npos && too_large_prefix == "old",
         "apply_patch rejects files that exceed its full-read bound before writing");

  auto unavailable_question = dispatcher.dispatch(ava::agent::ProviderToolCall{
      .id = "call_question_unavailable", .name = "question", .arguments_json = "{\"question\":\"Which approach?\"}"});
  expect(unavailable_question && !unavailable_question->success &&
             unavailable_question->result_text.find("unavailable") != std::string::npos,
         "question tool fails closed when no backend resolver is supplied");

  int question_prompts = 0;
  const ava::agent::ToolDispatcher question_dispatcher(ava::tools::ToolContext{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .question_resolver = [&question_prompts](const ava::agent::QuestionPrompt& prompt)
          -> ava::core::Result<ava::agent::QuestionAnswer> {
        ++question_prompts;
        expect(prompt.header == "Choose" && prompt.question == "Which approach?",
               "question resolver receives prompt text");
        expect(prompt.options.size() == 2 && prompt.options[0].value == "safe" && prompt.options[0].label == "Safe",
               "question resolver receives structured options");
        expect(!prompt.multiple && !prompt.allow_custom, "question resolver receives default selection flags");
        return ava::agent::QuestionAnswer{.selected_options = {"safe"}, .custom_text = ""};
      }});
  auto question = question_dispatcher.dispatch(
      ava::agent::ProviderToolCall{.id = "call_question",
                                   .name = "question",
                                   .arguments_json = "{\"header\":\"Choose\",\"question\":\"Which "
                                                     "approach?\",\"options\":[{\"value\":\"safe\",\"label\":\"Safe\"},"
                                                     "{\"value\":\"fast\",\"label\":\"Fast\"}]}"});
  expect(question && question->success && question_prompts == 1 &&
             question->result_text.find("\"selected_options\":[\"safe\"]") != std::string::npos,
         "question tool calls resolver and serializes selected answer");

  const ava::agent::ToolDispatcher multi_question_dispatcher(ava::tools::ToolContext{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .question_resolver =
          [](const ava::agent::QuestionPrompt& prompt) -> ava::core::Result<ava::agent::QuestionAnswer> {
        expect(prompt.multiple && prompt.allow_custom, "question resolver receives multi/custom flags");
        expect(prompt.options.size() == 2 && prompt.options[1].value == "Beta",
               "question resolver accepts string options");
        return ava::agent::QuestionAnswer{.selected_options = {"alpha", "Beta"}, .custom_text = "Use both"};
      }});
  auto multi_question = multi_question_dispatcher.dispatch(
      ava::agent::ProviderToolCall{.id = "call_question_multi",
                                   .name = "question",
                                   .arguments_json = "{\"question\":\"Pick "
                                                     "options\",\"options\":[{\"value\":\"alpha\",\"label\":\"Alpha\"},"
                                                     "\"Beta\"],\"multiple\":true,\"custom\":true}"});
  expect(multi_question && multi_question->success &&
             multi_question->result_text.find("\"selected_options\":[\"alpha\",\"Beta\"]") != std::string::npos &&
             multi_question->result_text.find("\"custom_text\":\"Use both\"") != std::string::npos,
         "question tool serializes multi-select and custom resolver answers");

  const ava::agent::ToolDispatcher too_many_answers_dispatcher(ava::tools::ToolContext{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .question_resolver = [](const ava::agent::QuestionPrompt&) -> ava::core::Result<ava::agent::QuestionAnswer> {
        return ava::agent::QuestionAnswer{.selected_options = std::vector<std::string>(65, "option"),
                                          .custom_text = ""};
      }});
  auto too_many_answers = too_many_answers_dispatcher.dispatch(ava::agent::ProviderToolCall{
      .id = "call_question_too_many_answers", .name = "question", .arguments_json = "{\"question\":\"Pick\"}"});
  expect(too_many_answers && !too_many_answers->success &&
             too_many_answers->result_text.find("too many selected options") != std::string::npos,
         "question tool rejects resolver answers with too many selected options");

  const std::string oversized_answer_text(9000, 'x');
  const ava::agent::ToolDispatcher oversized_selected_dispatcher(ava::tools::ToolContext{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .question_resolver =
          [&oversized_answer_text](const ava::agent::QuestionPrompt&) -> ava::core::Result<ava::agent::QuestionAnswer> {
        return ava::agent::QuestionAnswer{.selected_options = {oversized_answer_text}, .custom_text = ""};
      }});
  auto oversized_selected = oversized_selected_dispatcher.dispatch(ava::agent::ProviderToolCall{
      .id = "call_question_oversized_selected", .name = "question", .arguments_json = "{\"question\":\"Pick\"}"});
  expect(oversized_selected && !oversized_selected->success &&
             oversized_selected->result_text.find("selected option is too long") != std::string::npos,
         "question tool rejects oversized resolver selected option strings");

  const ava::agent::ToolDispatcher oversized_custom_dispatcher(ava::tools::ToolContext{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .question_resolver =
          [&oversized_answer_text](const ava::agent::QuestionPrompt&) -> ava::core::Result<ava::agent::QuestionAnswer> {
        return ava::agent::QuestionAnswer{.selected_options = {}, .custom_text = oversized_answer_text};
      }});
  auto oversized_custom = oversized_custom_dispatcher.dispatch(ava::agent::ProviderToolCall{
      .id = "call_question_oversized_custom", .name = "question", .arguments_json = "{\"question\":\"Pick\"}"});
  expect(oversized_custom && !oversized_custom->success &&
             oversized_custom->result_text.find("custom text is too long") != std::string::npos,
         "question tool rejects oversized resolver custom text");

  const ava::agent::ToolDispatcher failing_question_dispatcher(ava::tools::ToolContext{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .question_resolver = [](const ava::agent::QuestionPrompt&) -> ava::core::Result<ava::agent::QuestionAnswer> {
        return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Tool, "question UI unavailable"));
      }});
  auto failed_question = failing_question_dispatcher.dispatch(ava::agent::ProviderToolCall{
      .id = "call_question_failed", .name = "question", .arguments_json = "{\"question\":\"Continue?\"}"});
  expect(failed_question && !failed_question->success &&
             failed_question->result_text.find("question UI unavailable") != std::string::npos,
         "question tool returns resolver errors as backend failures");

  auto nul_question = dispatcher.dispatch(ava::agent::ProviderToolCall{
      .id = "call_nul_question", .name = "question", .arguments_json = "{\"question\":\"bad\\u0000question\"}"});
  expect(nul_question && !nul_question->success && nul_question->result_text.find("control byte") != std::string::npos,
         "question tool rejects NUL bytes in question text as control bytes");

  auto control_header = dispatcher.dispatch(
      ava::agent::ProviderToolCall{.id = "call_control_header",
                                   .name = "question",
                                   .arguments_json = "{\"header\":\"bad\\u001B\",\"question\":\"Ok?\"}"});
  expect(control_header && !control_header->success &&
             control_header->result_text.find("control byte") != std::string::npos,
         "question tool rejects control bytes in header text");

  auto control_option_value = dispatcher.dispatch(ava::agent::ProviderToolCall{
      .id = "call_control_option_value",
      .name = "question",
      .arguments_json = "{\"question\":\"Pick\",\"options\":[{\"value\":\"bad\\u001F\",\"label\":\"Bad\"}]}"});
  expect(control_option_value && !control_option_value->success &&
             control_option_value->result_text.find("control byte") != std::string::npos,
         "question tool rejects control bytes in option values");

  auto control_option_label = dispatcher.dispatch(ava::agent::ProviderToolCall{
      .id = "call_control_option_label",
      .name = "question",
      .arguments_json = "{\"question\":\"Pick\",\"options\":[{\"value\":\"bad\",\"label\":\"Bad\\u007F\"}]}"});
  expect(control_option_label && !control_option_label->success &&
             control_option_label->result_text.find("control byte") != std::string::npos,
         "question tool rejects control bytes in option labels");

  auto trailing_comma_question = dispatcher.dispatch(
      ava::agent::ProviderToolCall{.id = "call_question_trailing_comma",
                                   .name = "question",
                                   .arguments_json = "{\"question\":\"Pick\",\"options\":[\"A\",]}"});
  expect(trailing_comma_question && !trailing_comma_question->success &&
             trailing_comma_question->result_text.find("options array is malformed") != std::string::npos,
         "question tool rejects trailing commas in options arrays");

  auto malformed_question = question_dispatcher.dispatch(
      ava::agent::ProviderToolCall{.id = "call_question_malformed",
                                   .name = "question",
                                   .arguments_json = "{\"question\":\"Bad options\",\"options\":\"not-an-array\"}"});
  expect(malformed_question && !malformed_question->success &&
             malformed_question->result_text.find("options must be an array") != std::string::npos,
         "question tool rejects malformed option arguments before resolver dispatch");

  auto unknown = dispatcher.dispatch(
      ava::agent::ProviderToolCall{.id = "call_unknown", .name = "missing_tool", .arguments_json = "{}"});
  expect(unknown && !unknown->success && unknown->result_text.find("unknown tool") != std::string::npos,
         "tool dispatcher returns structured unknown tool errors");

  const auto schemas = ava::agent::ToolDispatcher::tool_schemas_json();
  bool has_apply_patch = false;
  bool has_question = false;
  bool question_has_allow_multiple = false;
  for (const auto& schema : schemas) {
    has_apply_patch = has_apply_patch || schema.find("apply_patch") != std::string::npos;
    const bool is_question_schema = schema.find("\"name\":\"question\"") != std::string::npos;
    has_question = has_question || is_question_schema;
    question_has_allow_multiple =
        question_has_allow_multiple || (is_question_schema && schema.find("allow_multiple") != std::string::npos);
  }
  expect(!schemas.empty() && schemas[0].find("read_file") != std::string::npos && has_apply_patch && has_question,
         "tool dispatcher exposes provider tool schemas");
  expect(question_has_allow_multiple, "question tool schema exposes the allow_multiple alias");
}

void test_tool_dispatcher_plan_mode_denies_mutation() {
  const auto root = temp_root() / "dispatcher-plan";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  std::filesystem::create_directories(workspace);

  const ava::agent::ToolDispatcher dispatcher(
      ava::tools::ToolContext{.workspace_dir = workspace, .mode = ava::agent::Mode::Plan});
  auto denied = dispatcher.dispatch(ava::agent::ProviderToolCall{
      .id = "call_write", .name = "write_file", .arguments_json = "{\"path\":\"main.cpp\",\"content\":\"bad\"}"});
  expect(denied && !denied->success && denied->result_text.find("permission_denied") != std::string::npos,
         "tool dispatcher keeps plan mode source mutation denied inside tools");
  expect(!std::filesystem::exists(workspace / "main.cpp"), "denied plan mode write does not create source file");
}

void test_agent_loop_text_only_turn() {
  const auto root = temp_root() / "agent-text";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  ava::session::SessionStore store(ava::session::SessionStoreOptions{
      .root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "text"});
  const ava::provider::OpenAIProvider provider("https://api.example.test");
  ava::tests::FakeTransport transport(
      {sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"hello user\"}\n\n"
                    "data: [DONE]\n\n")});
  ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{.workspace_dir = workspace,
                                                          .mode = ava::agent::Mode::Build,
                                                          .provider_id = "openai",
                                                          .model_id = "gpt-5.5",
                                                          .system_prompt = "system prompt",
                                                          .access_token = "token",
                                                          .openai_oauth = true,
                                                          .openai_account_id = "acct_123"});
  auto result = loop.run_turn("hi", store, provider, transport);
  expect(result && result->final_text == "hello user" && result->tool_calls == 0 &&
             result->initial_context_messages == 1 && !result->used_compacted_context && result->tool_iterations == 0 &&
             result->stop_reason == "completed",
         "agent loop returns text-only provider response with status metadata");
  expect(transport.requests().size() == 1 && transport.requests()[0].body.find("read_file") != std::string::npos,
         "agent loop includes tool schemas in provider request");
  expect(transport.requests().size() == 1 &&
             transport.requests()[0].url == "https://chatgpt.com/backend-api/codex/responses" &&
             transport.requests()[0].headers.at("ChatGPT-Account-Id") == "acct_123",
         "agent loop routes OpenAI OAuth turns through Codex endpoint");
  expect(transport.requests().size() == 1 && transport.requests()[0].body.find("\"store\":false") != std::string::npos,
         "agent loop disables Codex response storage for OpenAI OAuth turns");
  auto entries = store.load();
  expect(entries && entries->size() == 2 && (*entries)[0].type == ava::session::EntryType::UserMessage &&
             (*entries)[1].type == ava::session::EntryType::AssistantMessage,
         "agent loop persists user and assistant entries for text-only turn");
}

void test_agent_loop_tool_turn_and_continuation() {
  const auto root = temp_root() / "agent-tool";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  {
    std::ofstream file(workspace / "note.txt", std::ios::binary | std::ios::trunc);
    file << "tool content";
  }
  ava::session::SessionStore store(ava::session::SessionStoreOptions{
      .root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "tool"});
  const ava::provider::OpenAIProvider provider("https://api.example.test");
  ava::tests::FakeTransport transport(
      {sse_response(
           "data: {\"type\":\"response.function_call.added\",\"item_id\":\"call_1\",\"name\":\"read_file\"}\n\n"
           "data: "
           "{\"type\":\"response.function_call_arguments.delta\",\"item_id\":\"call_1\",\"delta\":\"{\\\"path\\\":"
           "\\\"note.txt\\\"}\"}\n\n"
           "data: {\"type\":\"response.function_call.done\",\"item_id\":\"call_1\"}\n\n"
           "data: [DONE]\n\n"),
       sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"read it\"}\n\n"
                    "data: [DONE]\n\n")});
  std::vector<ava::agent::ToolTimelineEntry> tool_events;
  ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .provider_id = "openai",
      .model_id = "gpt-5.5",
      .system_prompt = "system prompt",
      .access_token = "token",
      .on_tool_event = [&tool_events](const auto& entry) { tool_events.push_back(entry); }});
  auto result = loop.run_turn("read note", store, provider, transport);
  expect(result && result->final_text == "read it" && result->tool_calls == 1 && result->provider_iterations == 2 &&
             result->initial_context_messages == 1 && result->tool_iterations == 1 &&
             result->stop_reason == "completed",
         "agent loop runs one sequential tool call then continues to final answer with status metadata");
  expect(transport.requests().size() == 2 && transport.requests()[1].body.find("tool content") != std::string::npos,
         "agent loop sends persisted tool result as continuation context");
  expect(result && result->tool_timeline.size() == 1 &&
             result->tool_timeline.front().status == ava::agent::ToolTimelineStatus::Success &&
             result->tool_timeline.front().name == "read_file" &&
             result->tool_timeline.front().argument_summary.find("path=note.txt") != std::string::npos &&
             result->tool_timeline.front().argument_summary.find('{') == std::string::npos &&
             result->tool_timeline.front().result_summary.find("tool content") == std::string::npos &&
             result->tool_timeline.front().result_summary.find("bytes") != std::string::npos,
         "agent loop returns safe compact tool timeline summaries");
  expect(tool_events.size() == 2 && tool_events.front().status == ava::agent::ToolTimelineStatus::Running &&
             tool_events.back().status == ava::agent::ToolTimelineStatus::Success,
         "agent loop publishes running and completed tool timeline events");

  auto entries = store.load();
  expect(entries.has_value(), "agent tool turn session loads");
  if (!entries) return;
  bool saw_tool_call = false;
  bool saw_tool_result = false;
  bool saw_final_assistant = false;
  for (const auto& entry : *entries) {
    saw_tool_call = saw_tool_call || entry.type == ava::session::EntryType::ToolCall;
    saw_tool_result = saw_tool_result || entry.type == ava::session::EntryType::ToolResult;
    saw_final_assistant = saw_final_assistant || (entry.type == ava::session::EntryType::AssistantMessage &&
                                                  entry.data_json.find("read it") != std::string::npos);
  }
  expect(saw_tool_call && saw_tool_result && saw_final_assistant,
         "agent loop persists assistant, tool call, and tool result entries");
}

void test_agent_loop_permission_resolver_threads_to_tools() {
  const auto root = temp_root() / "agent-permission-resolver";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  const auto outside_path = root / "outside.txt";
  {
    std::ofstream file(outside_path, std::ios::binary | std::ios::trunc);
    file << "outside via agent";
  }
  ava::session::SessionStore store(ava::session::SessionStoreOptions{
      .root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "resolver"});
  const ava::provider::OpenAIProvider provider("https://api.example.test");
  ava::tests::FakeTransport transport(
      {sse_response(
           "data: {\"type\":\"response.function_call.added\",\"item_id\":\"call_outside\",\"name\":\"read_file\"}\n\n"
           "data: "
           "{\"type\":\"response.function_call_arguments.delta\",\"item_id\":\"call_outside\",\"delta\":\"{"
           "\\\"path\\\":\\\"" +
           ava::core::json::escape(outside_path.generic_string()) +
           "\\\"}\"}\n\n"
           "data: [DONE]\n\n"),
       sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"used resolver\"}\n\n"
                    "data: [DONE]\n\n")});
  int prompts = 0;
  ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .provider_id = "openai",
      .model_id = "gpt-5.5",
      .system_prompt = "system prompt",
      .access_token = "token",
      .permission_resolver = [&prompts, &outside_path](const ava::permissions::PermissionPrompt& prompt)
          -> ava::core::Result<ava::permissions::PermissionResolution> {
        ++prompts;
        expect(prompt.target_path == outside_path, "agent loop resolver sees tool target path");
        return ava::permissions::PermissionResolution::Allow;
      }});
  auto result = loop.run_turn("read outside", store, provider, transport);
  expect(result && result->final_text == "used resolver" && prompts == 1 && result->tool_timeline.size() == 1 &&
             result->tool_timeline.front().status == ava::agent::ToolTimelineStatus::Success,
         "agent loop threads permission resolver into tool dispatcher");
  expect(
      transport.requests().size() == 2 && transport.requests()[1].body.find("outside via agent") != std::string::npos,
      "agent loop continuation includes resolver-approved tool result");
  auto resolver_entries = store.load();
  auto resolver_audits =
      resolver_entries ? permission_entries(*resolver_entries) : std::vector<ava::session::SessionEntry>{};
  expect(resolver_audits.size() == 2 &&
             ava::core::json::string_field(resolver_audits[0].data_json, "action") == "ask" &&
             ava::core::json::string_field(resolver_audits[0].data_json, "resolution_source") == "policy" &&
             ava::core::json::string_field(resolver_audits[1].data_json, "resolution") == "allow" &&
             ava::core::json::string_field(resolver_audits[1].data_json, "resolution_source") == "resolver",
         "agent loop persists ask and resolver permission audit entries");

  {
    const auto bash_root = temp_root() / "agent-bash-ask-allow";
    std::filesystem::remove_all(bash_root, remove_error);
    const auto bash_workspace = bash_root / "workspace";
    std::filesystem::create_directories(bash_workspace);
    ava::session::SessionStore bash_store(ava::session::SessionStoreOptions{
        .root_dir = bash_root / "sessions", .workspace_dir = bash_workspace, .session_id = "bash-allow"});
    ava::tests::FakeTransport bash_transport(
        {sse_response(
             "data: {\"type\":\"response.function_call.added\",\"item_id\":\"call_bash\",\"name\":\"bash\"}\n\n"
             "data: "
             "{\"type\":\"response.function_call_arguments.delta\",\"item_id\":\"call_bash\",\"delta\":\"{"
             "\\\"command\\\":\\\"true\\\"}\"}\n\n"
             "data: [DONE]\n\n"),
         sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"bash allowed\"}\n\n"
                      "data: [DONE]\n\n")});
    int bash_allow_prompts = 0;
    ava::agent::AgentLoop bash_loop(ava::agent::AgentLoopOptions{
        .workspace_dir = bash_workspace,
        .mode = ava::agent::Mode::Build,
        .provider_id = "openai",
        .model_id = "gpt-5.5",
        .system_prompt = "system prompt",
        .access_token = "token",
        .permission_resolver = [&bash_allow_prompts](const ava::permissions::PermissionPrompt& prompt)
            -> ava::core::Result<ava::permissions::PermissionResolution> {
          ++bash_allow_prompts;
          expect(prompt.operation == ava::permissions::Operation::RunCommand,
                 "agent bash allow resolver sees run command");
          expect(prompt.command == "true", "agent bash allow resolver sees command text");
          return ava::permissions::PermissionResolution::Allow;
        }});
    auto bash_result = bash_loop.run_turn("run true", bash_store, provider, bash_transport);
    expect(bash_result && bash_result->final_text == "bash allowed" && bash_allow_prompts == 1 &&
               bash_result->tool_timeline.size() == 1 &&
               bash_result->tool_timeline.front().status == ava::agent::ToolTimelineStatus::Success,
           "agent loop allows bash Ask decisions when resolver allows once");
  }

  {
    const auto bash_root = temp_root() / "agent-bash-ask-deny";
    std::filesystem::remove_all(bash_root, remove_error);
    const auto bash_workspace = bash_root / "workspace";
    std::filesystem::create_directories(bash_workspace);
    ava::session::SessionStore bash_store(ava::session::SessionStoreOptions{
        .root_dir = bash_root / "sessions", .workspace_dir = bash_workspace, .session_id = "bash-deny"});
    ava::tests::FakeTransport bash_transport(
        {sse_response(
             "data: {\"type\":\"response.function_call.added\",\"item_id\":\"call_bash\",\"name\":\"bash\"}\n\n"
             "data: "
             "{\"type\":\"response.function_call_arguments.delta\",\"item_id\":\"call_bash\",\"delta\":\"{"
             "\\\"command\\\":\\\"true\\\"}\"}\n\n"
             "data: [DONE]\n\n"),
         sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"bash denied\"}\n\n"
                      "data: [DONE]\n\n")});
    int bash_deny_prompts = 0;
    ava::agent::AgentLoop bash_loop(ava::agent::AgentLoopOptions{
        .workspace_dir = bash_workspace,
        .mode = ava::agent::Mode::Build,
        .provider_id = "openai",
        .model_id = "gpt-5.5",
        .system_prompt = "system prompt",
        .access_token = "token",
        .permission_resolver = [&bash_deny_prompts](const ava::permissions::PermissionPrompt& prompt)
            -> ava::core::Result<ava::permissions::PermissionResolution> {
          ++bash_deny_prompts;
          expect(prompt.operation == ava::permissions::Operation::RunCommand,
                 "agent bash deny resolver sees run command");
          return ava::permissions::PermissionResolution::Deny;
        }});
    auto bash_result = bash_loop.run_turn("run true", bash_store, provider, bash_transport);
    expect(bash_result && bash_result->final_text == "bash denied" && bash_deny_prompts == 1 &&
               bash_result->tool_timeline.size() == 1 &&
               bash_result->tool_timeline.front().status == ava::agent::ToolTimelineStatus::Error,
           "agent loop records denied bash Ask decisions as failed tool results and continues");
    auto bash_entries = bash_store.load();
    auto bash_audits = bash_entries ? permission_entries(*bash_entries) : std::vector<ava::session::SessionEntry>{};
    expect(bash_audits.size() == 2 && ava::core::json::string_field(bash_audits[1].data_json, "command") == "true" &&
               ava::core::json::string_field(bash_audits[1].data_json, "resolution") == "deny" &&
               ava::core::json::string_field(bash_audits[1].data_json, "resolution_source") == "resolver",
           "agent loop persists resolver-denied command permission audit entries");
  }

  {
    const auto bash_root = temp_root() / "agent-bash-ask-fail";
    std::filesystem::remove_all(bash_root, remove_error);
    const auto bash_workspace = bash_root / "workspace";
    std::filesystem::create_directories(bash_workspace);
    ava::session::SessionStore bash_store(ava::session::SessionStoreOptions{
        .root_dir = bash_root / "sessions", .workspace_dir = bash_workspace, .session_id = "bash-fail"});
    ava::tests::FakeTransport bash_transport(
        {sse_response(
             "data: {\"type\":\"response.function_call.added\",\"item_id\":\"call_bash\",\"name\":\"bash\"}\n\n"
             "data: "
             "{\"type\":\"response.function_call_arguments.delta\",\"item_id\":\"call_bash\",\"delta\":\"{"
             "\\\"command\\\":\\\"true\\\"}\"}\n\n"
             "data: [DONE]\n\n"),
         sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"bash resolver failed\"}\n\n"
                      "data: [DONE]\n\n")});
    int bash_fail_prompts = 0;
    ava::agent::AgentLoop bash_loop(ava::agent::AgentLoopOptions{
        .workspace_dir = bash_workspace,
        .mode = ava::agent::Mode::Build,
        .provider_id = "openai",
        .model_id = "gpt-5.5",
        .system_prompt = "system prompt",
        .access_token = "token",
        .permission_resolver = [&bash_fail_prompts](const ava::permissions::PermissionPrompt& prompt)
            -> ava::core::Result<ava::permissions::PermissionResolution> {
          ++bash_fail_prompts;
          expect(prompt.operation == ava::permissions::Operation::RunCommand,
                 "agent bash fail resolver sees run command");
          return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Io, "resolver failed"));
        }});
    auto bash_result = bash_loop.run_turn("run true", bash_store, provider, bash_transport);
    expect(bash_result && bash_result->final_text == "bash resolver failed" && bash_fail_prompts == 1 &&
               bash_result->tool_timeline.size() == 1 &&
               bash_result->tool_timeline.front().status == ava::agent::ToolTimelineStatus::Error &&
               bash_transport.requests().size() == 2,
           "agent loop records failed bash Ask resolver as failed tool result and continues");
  }
}

void test_agent_loop_question_resolver_threads_to_tools() {
  const auto root = temp_root() / "agent-question-resolver";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  ava::session::SessionStore store(ava::session::SessionStoreOptions{
      .root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "question-resolver"});
  const ava::provider::OpenAIProvider provider("https://api.example.test");
  ava::tests::FakeTransport transport(
      {sse_response(
           "data: {\"type\":\"response.function_call.added\",\"item_id\":\"call_question\",\"name\":\"question\"}\n\n"
           "data: "
           "{\"type\":\"response.function_call_arguments.delta\",\"item_id\":\"call_question\",\"delta\":\"{"
           "\\\"question\\\":\\\"Pick one?\\\",\\\"options\\\":[\\\"A\\\",\\\"B\\\"]}\"}\n\n"
           "data: [DONE]\n\n"),
       sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"question answered\"}\n\n"
                    "data: [DONE]\n\n")});
  int prompts = 0;
  ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{
      .workspace_dir = workspace,
      .mode = ava::agent::Mode::Build,
      .provider_id = "openai",
      .model_id = "gpt-5.5",
      .system_prompt = "system prompt",
      .access_token = "token",
      .question_resolver =
          [&prompts](const ava::agent::QuestionPrompt& prompt) -> ava::core::Result<ava::agent::QuestionAnswer> {
        ++prompts;
        expect(prompt.question == "Pick one?" && prompt.options.size() == 2,
               "agent loop question resolver receives provider prompt");
        return ava::agent::QuestionAnswer{.selected_options = {"B"}, .custom_text = ""};
      }});
  auto result = loop.run_turn("ask", store, provider, transport);
  expect(result && result->final_text == "question answered" && prompts == 1 && result->tool_timeline.size() == 1 &&
             result->tool_timeline.front().status == ava::agent::ToolTimelineStatus::Success,
         "agent loop threads question resolver into tool dispatcher");
  expect(transport.requests().size() == 2 &&
             transport.requests()[1].body.find("\\\"selected_options\\\":[\\\"B\\\"]") != std::string::npos,
         "agent loop continuation includes serialized question answer");
}

void test_agent_loop_non_stream_response() {
  const auto root = temp_root() / "agent-non-stream";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  ava::session::SessionStore store(ava::session::SessionStoreOptions{
      .root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "nonstream"});
  const ava::provider::OpenAIProvider provider("https://api.example.test");
  ava::tests::FakeTransport transport({ava::provider::HttpResponse{
      .status_code = 200, .headers = {}, .body = "{\"output_text\":\"plain response with data: literal\"}"}});
  ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{.workspace_dir = workspace,
                                                          .mode = ava::agent::Mode::Build,
                                                          .provider_id = "openai",
                                                          .model_id = "gpt-5.5",
                                                          .system_prompt = "system prompt",
                                                          .access_token = "token",
                                                          .stream = false});
  auto result = loop.run_turn("hi", store, provider, transport);
  expect(result && result->final_text == "plain response with data: literal",
         "agent loop parses non-stream response without sniffing data text");
  expect(!transport.requests().empty() && transport.requests()[0].body.find("\"stream\":false") != std::string::npos,
         "agent loop passes explicit non-stream request expectation");
}

void test_agent_loop_compaction_status_metadata() {
  const auto root = temp_root() / "agent-compaction-status";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  ava::session::SessionStore store(ava::session::SessionStoreOptions{
      .root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "compaction-status"});
  auto appended = store.append(ava::session::SessionEntry{.id = "entry_compaction_status",
                                                          .parent_id = "",
                                                          .type = ava::session::EntryType::Compaction,
                                                          .timestamp = ava::session::now_timestamp(),
                                                          .data_json = "{\"summary\":\"older context\"}"});
  expect(appended.has_value(), "agent loop compaction metadata test seeds compaction entry");
  const ava::provider::OpenAIProvider provider("https://api.example.test");
  ava::tests::FakeTransport transport(
      {sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"after compaction\"}\n\n"
                    "data: [DONE]\n\n")});
  ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{.workspace_dir = workspace,
                                                          .mode = ava::agent::Mode::Build,
                                                          .provider_id = "openai",
                                                          .model_id = "gpt-5.5",
                                                          .system_prompt = "system prompt",
                                                          .access_token = "token"});
  auto result = loop.run_turn("continue", store, provider, transport);
  expect(result && result->used_compacted_context && result->initial_context_messages == 2 &&
             result->stop_reason == "completed",
         "agent loop status metadata reports compacted initial provider context");
  expect(transport.requests().size() == 1 &&
             transport.requests()[0].body.find("Compacted prior conversation summary") != std::string::npos,
         "agent loop sends compacted context in initial provider request");
}

void test_agent_loop_cancellation_boundaries() {
  const ava::provider::OpenAIProvider provider("https://api.example.test");

  {
    const auto root = temp_root() / "agent-cancel-before-turn-start";
    std::error_code remove_error;
    std::filesystem::remove_all(root, remove_error);
    const auto workspace = root / "workspace";
    std::filesystem::create_directories(workspace);
    ava::session::SessionStore store(ava::session::SessionStoreOptions{
        .root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "cancel-before-turn-start"});
    ava::tests::FakeTransport transport(
        {sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"should not send\"}\n\n"
                      "data: [DONE]\n\n")});
    ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{.workspace_dir = workspace,
                                                            .mode = ava::agent::Mode::Build,
                                                            .provider_id = "openai",
                                                            .model_id = "gpt-5.5",
                                                            .system_prompt = "system prompt",
                                                            .access_token = "token",
                                                            .cancel_requested = [] { return true; }});
    auto result = loop.run_turn("cancel now", store, provider, transport);
    auto entries = store.load();
    bool saw_user_message = false;
    bool saw_cancel = false;
    if (entries) {
      for (const auto& entry : *entries) {
        saw_user_message = saw_user_message || entry.type == ava::session::EntryType::UserMessage;
        saw_cancel = saw_cancel || (entry.type == ava::session::EntryType::Cancel &&
                                    entry.data_json.find("before_turn_start") != std::string::npos);
      }
    }
    expect(!result && result.error().message() == "agent loop canceled" && transport.requests().empty() && entries &&
               saw_cancel && !saw_user_message,
           "agent loop cancellation before turn start avoids persisting the user message");
  }

  {
    const auto root = temp_root() / "agent-cancel-before-provider";
    std::error_code remove_error;
    std::filesystem::remove_all(root, remove_error);
    const auto workspace = root / "workspace";
    std::filesystem::create_directories(workspace);
    ava::session::SessionStore store(ava::session::SessionStoreOptions{
        .root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "cancel-before-provider"});
    ava::tests::FakeTransport transport(
        {sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"should not send\"}\n\n"
                      "data: [DONE]\n\n")});
    int cancel_checks = 0;
    ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{.workspace_dir = workspace,
                                                            .mode = ava::agent::Mode::Build,
                                                            .provider_id = "openai",
                                                            .model_id = "gpt-5.5",
                                                            .system_prompt = "system prompt",
                                                            .access_token = "token",
                                                            .cancel_requested = [&cancel_checks] {
                                                              ++cancel_checks;
                                                              return cancel_checks >= 2;
                                                            }});
    auto result = loop.run_turn("cancel before provider", store, provider, transport);
    auto entries = store.load();
    const bool saw_cancel = entries && std::ranges::any_of(*entries, [](const ava::session::SessionEntry& entry) {
                              return entry.type == ava::session::EntryType::Cancel &&
                                     entry.data_json.find("before_provider_call") != std::string::npos;
                            });
    const bool saw_user_message = entries && std::ranges::any_of(*entries, [](const ava::session::SessionEntry& entry) {
                                    return entry.type == ava::session::EntryType::UserMessage;
                                  });
    expect(!result && result.error().message() == "agent loop canceled" && transport.requests().empty() && saw_cancel &&
               saw_user_message,
           "agent loop cancellation before provider call avoids transport send and records cancel boundary");
  }

  {
    const auto root = temp_root() / "agent-cancel-before-tool";
    std::error_code remove_error;
    std::filesystem::remove_all(root, remove_error);
    const auto workspace = root / "workspace";
    std::filesystem::create_directories(workspace);
    {
      std::ofstream file(workspace / "note.txt", std::ios::binary | std::ios::trunc);
      file << "must not read";
    }
    ava::session::SessionStore store(ava::session::SessionStoreOptions{
        .root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "cancel-before-tool"});
    bool cancel = false;
    CallbackTransport transport(
        {sse_response(
            "data: {\"type\":\"response.function_call.added\",\"item_id\":\"call_read\",\"name\":\"read_file\"}\n\n"
            "data: "
            "{\"type\":\"response.function_call_arguments.delta\",\"item_id\":\"call_read\",\"delta\":\"{\\\"path\\\":"
            "\\\"note.txt\\\"}\"}\n\n"
            "data: [DONE]\n\n")},
        [&cancel] { cancel = true; });
    ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{.workspace_dir = workspace,
                                                            .mode = ava::agent::Mode::Build,
                                                            .provider_id = "openai",
                                                            .model_id = "gpt-5.5",
                                                            .system_prompt = "system prompt",
                                                            .access_token = "token",
                                                            .cancel_requested = [&cancel] { return cancel; }});
    auto result = loop.run_turn("read then cancel", store, provider, transport);
    auto entries = store.load();
    bool saw_tool_entry = false;
    bool saw_cancel = false;
    if (entries) {
      for (const auto& entry : *entries) {
        saw_tool_entry = saw_tool_entry || entry.type == ava::session::EntryType::ToolCall ||
                         entry.type == ava::session::EntryType::ToolResult;
        saw_cancel = saw_cancel || (entry.type == ava::session::EntryType::Cancel &&
                                    (entry.data_json.find("before_tool_dispatch") != std::string::npos ||
                                     entry.data_json.find("after_provider_call") != std::string::npos));
      }
    }
    expect(!result && result.error().message() == "agent loop canceled" && transport.requests().size() == 1 &&
               entries && saw_cancel && !saw_tool_entry,
           "agent loop cancellation before tool dispatch avoids tool call/result entries");
  }
}

void test_agent_loop_error_paths_and_bounds() {
  const ava::provider::OpenAIProvider provider("https://api.example.test");

  {
    const auto root = temp_root() / "agent-provider-error";
    std::error_code remove_error;
    std::filesystem::remove_all(root, remove_error);
    const auto workspace = root / "workspace";
    std::filesystem::create_directories(workspace);
    ava::session::SessionStore store(ava::session::SessionStoreOptions{
        .root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "provider-error"});
    ava::tests::FakeTransport transport(
        {sse_response("data: {\"type\":\"response.error\",\"error\":{\"message\":\"bad request\"}}\n\n")});
    ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{.workspace_dir = workspace,
                                                            .mode = ava::agent::Mode::Build,
                                                            .provider_id = "openai",
                                                            .model_id = "gpt-5.5",
                                                            .system_prompt = "system prompt",
                                                            .access_token = "token"});
    auto result = loop.run_turn("hi", store, provider, transport);
    expect(!result && result.error().message().find("provider stream error") != std::string::npos,
           "agent loop returns provider error events");
  }

  {
    const auto root = temp_root() / "agent-empty-transport";
    std::error_code remove_error;
    std::filesystem::remove_all(root, remove_error);
    const auto workspace = root / "workspace";
    std::filesystem::create_directories(workspace);
    ava::session::SessionStore store(ava::session::SessionStoreOptions{
        .root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "empty-transport"});
    ava::tests::FakeTransport transport({});
    ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{.workspace_dir = workspace,
                                                            .mode = ava::agent::Mode::Build,
                                                            .provider_id = "openai",
                                                            .model_id = "gpt-5.5",
                                                            .system_prompt = "system prompt",
                                                            .access_token = "token"});
    auto result = loop.run_turn("hi", store, provider, transport);
    expect(!result && result.error().message().find("fake transport has no response") != std::string::npos,
           "agent loop returns transport failures");
  }

  {
    const auto root = temp_root() / "agent-empty-response";
    std::error_code remove_error;
    std::filesystem::remove_all(root, remove_error);
    const auto workspace = root / "workspace";
    std::filesystem::create_directories(workspace);
    ava::session::SessionStore store(ava::session::SessionStoreOptions{
        .root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "empty-response"});
    ava::tests::FakeTransport transport({sse_response("")});
    ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{.workspace_dir = workspace,
                                                            .mode = ava::agent::Mode::Build,
                                                            .provider_id = "openai",
                                                            .model_id = "gpt-5.5",
                                                            .system_prompt = "system prompt",
                                                            .access_token = "token"});
    auto result = loop.run_turn("hi", store, provider, transport);
    expect(!result && result.error().message().find("empty") != std::string::npos,
           "agent loop returns empty provider responses");
  }

  {
    const auto root = temp_root() / "agent-event-bound";
    std::error_code remove_error;
    std::filesystem::remove_all(root, remove_error);
    const auto workspace = root / "workspace";
    std::filesystem::create_directories(workspace);
    ava::session::SessionStore store(ava::session::SessionStoreOptions{
        .root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "event-bound"});
    ava::tests::FakeTransport transport(
        {sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"a\"}\n\n"
                      "data: [DONE]\n\n")});
    ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{.workspace_dir = workspace,
                                                            .mode = ava::agent::Mode::Build,
                                                            .provider_id = "openai",
                                                            .model_id = "gpt-5.5",
                                                            .system_prompt = "system prompt",
                                                            .access_token = "token",
                                                            .max_provider_events = 1});
    auto result = loop.run_turn("hi", store, provider, transport);
    expect(!result && result.error().message().find("event limit") != std::string::npos,
           "agent loop enforces provider event bounds");
  }

  {
    const auto root = temp_root() / "agent-text-bound";
    std::error_code remove_error;
    std::filesystem::remove_all(root, remove_error);
    const auto workspace = root / "workspace";
    std::filesystem::create_directories(workspace);
    ava::session::SessionStore store(ava::session::SessionStoreOptions{
        .root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "text-bound"});
    ava::tests::FakeTransport transport(
        {sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"hello\"}\n\n"
                      "data: [DONE]\n\n")});
    ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{.workspace_dir = workspace,
                                                            .mode = ava::agent::Mode::Build,
                                                            .provider_id = "openai",
                                                            .model_id = "gpt-5.5",
                                                            .system_prompt = "system prompt",
                                                            .access_token = "token",
                                                            .max_assistant_text_bytes = 3});
    auto result = loop.run_turn("hi", store, provider, transport);
    expect(!result && result.error().message().find("text byte limit") != std::string::npos,
           "agent loop enforces assistant text byte bounds");
  }

  {
    const auto root = temp_root() / "agent-arg-bound";
    std::error_code remove_error;
    std::filesystem::remove_all(root, remove_error);
    const auto workspace = root / "workspace";
    std::filesystem::create_directories(workspace);
    ava::session::SessionStore store(ava::session::SessionStoreOptions{
        .root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "arg-bound"});
    ava::tests::FakeTransport transport({sse_response(
        "data: {\"type\":\"response.function_call.added\",\"item_id\":\"call_1\",\"name\":\"read_file\"}\n\n"
        "data: "
        "{\"type\":\"response.function_call_arguments.delta\",\"item_id\":\"call_1\",\"delta\":\"{\\\"path\\\":"
        "\\\"note.txt\\\"}\"}\n\n"
        "data: [DONE]\n\n")});
    ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{.workspace_dir = workspace,
                                                            .mode = ava::agent::Mode::Build,
                                                            .provider_id = "openai",
                                                            .model_id = "gpt-5.5",
                                                            .system_prompt = "system prompt",
                                                            .access_token = "token",
                                                            .max_tool_argument_bytes = 5});
    auto result = loop.run_turn("hi", store, provider, transport);
    expect(!result && result.error().message().find("argument byte limit") != std::string::npos,
           "agent loop enforces tool argument byte bounds");
  }

  {
    const auto root = temp_root() / "agent-control-call-id";
    std::error_code remove_error;
    std::filesystem::remove_all(root, remove_error);
    const auto workspace = root / "workspace";
    std::filesystem::create_directories(workspace);
    ava::session::SessionStore store(ava::session::SessionStoreOptions{
        .root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "control-call-id"});
    ava::tests::FakeTransport transport({sse_response(
        "data: {\"type\":\"response.function_call.added\",\"item_id\":\"call_\\u0001bad\",\"name\":\"read_file\"}\n\n"
        "data: [DONE]\n\n")});
    ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{.workspace_dir = workspace,
                                                            .mode = ava::agent::Mode::Build,
                                                            .provider_id = "openai",
                                                            .model_id = "gpt-5.5",
                                                            .system_prompt = "system prompt",
                                                            .access_token = "token"});
    auto result = loop.run_turn("bad id", store, provider, transport);
    auto entries = store.load();
    bool saw_tool_entry = false;
    if (entries) {
      saw_tool_entry = std::ranges::any_of(*entries, [](const ava::session::SessionEntry& entry) {
        return entry.type == ava::session::EntryType::ToolCall || entry.type == ava::session::EntryType::ToolResult;
      });
    }
    expect(!result && result.error().message().find("control byte") != std::string::npos && entries && !saw_tool_entry,
           "agent loop rejects provider tool call ids with control bytes before session or timeline use");
  }

  {
    const auto root = temp_root() / "agent-long-call-id";
    std::error_code remove_error;
    std::filesystem::remove_all(root, remove_error);
    const auto workspace = root / "workspace";
    std::filesystem::create_directories(workspace);
    ava::session::SessionStore store(ava::session::SessionStoreOptions{
        .root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "long-call-id"});
    const std::string long_call_id(300, 'a');
    ava::tests::FakeTransport transport(
        {sse_response("data: {\"type\":\"response.function_call.added\",\"item_id\":\"" + long_call_id +
                      "\",\"name\":\"read_file\"}\n\n"
                      "data: [DONE]\n\n")});
    ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{.workspace_dir = workspace,
                                                            .mode = ava::agent::Mode::Build,
                                                            .provider_id = "openai",
                                                            .model_id = "gpt-5.5",
                                                            .system_prompt = "system prompt",
                                                            .access_token = "token"});
    auto result = loop.run_turn("long id", store, provider, transport);
    expect(!result && result.error().message().find("too long") != std::string::npos,
           "agent loop rejects overlong provider tool call ids");
  }
}

void test_agent_loop_multiple_tools_and_denied_continuation() {
  const auto root = temp_root() / "agent-multi-tools";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  {
    std::ofstream one(workspace / "one.txt", std::ios::binary | std::ios::trunc);
    one << "one";
    std::ofstream two(workspace / "two.txt", std::ios::binary | std::ios::trunc);
    two << "two";
  }
  ava::session::SessionStore store(ava::session::SessionStoreOptions{
      .root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "multi"});
  const ava::provider::OpenAIProvider provider("https://api.example.test");
  ava::tests::FakeTransport transport(
      {sse_response(
           "data: {\"type\":\"response.function_call.added\",\"item_id\":\"call_1\",\"name\":\"read_file\"}\n\n"
           "data: "
           "{\"type\":\"response.function_call_arguments.delta\",\"item_id\":\"call_1\",\"delta\":\"{\\\"path\\\":"
           "\\\"one.txt\\\"}\"}\n\n"
           "data: {\"type\":\"response.function_call.done\",\"item_id\":\"call_1\"}\n\n"
           "data: {\"type\":\"response.function_call.added\",\"item_id\":\"call_2\",\"name\":\"read_file\"}\n\n"
           "data: "
           "{\"type\":\"response.function_call_arguments.delta\",\"item_id\":\"call_2\",\"delta\":\"{\\\"path\\\":"
           "\\\"two.txt\\\"}\"}\n\n"
           "data: {\"type\":\"response.function_call.done\",\"item_id\":\"call_2\"}\n\n"
           "data: [DONE]\n\n"),
       sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"done\"}\n\n"
                    "data: [DONE]\n\n")});
  ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{.workspace_dir = workspace,
                                                          .mode = ava::agent::Mode::Build,
                                                          .provider_id = "openai",
                                                          .model_id = "gpt-5.5",
                                                          .system_prompt = "system prompt",
                                                          .access_token = "token"});
  auto result = loop.run_turn("read both", store, provider, transport);
  expect(result && result->tool_calls == 2 && result->final_text == "done",
         "agent loop handles multiple tool calls before continuation");

  const auto denied_root = temp_root() / "agent-denied-continuation";
  std::filesystem::remove_all(denied_root, remove_error);
  const auto denied_workspace = denied_root / "workspace";
  std::filesystem::create_directories(denied_workspace);
  ava::session::SessionStore denied_store(ava::session::SessionStoreOptions{
      .root_dir = denied_root / "sessions", .workspace_dir = denied_workspace, .session_id = "denied"});
  ava::tests::FakeTransport denied_transport(
      {sse_response(
           "data: {\"type\":\"response.function_call.added\",\"item_id\":\"call_write\",\"name\":\"write_file\"}\n\n"
           "data: "
           "{\"type\":\"response.function_call_arguments.delta\",\"item_id\":\"call_write\",\"delta\":\"{\\\"path\\\":"
           "\\\"src/new.cpp\\\",\\\"content\\\":\\\"bad\\\"}\"}\n\n"
           "data: [DONE]\n\n"),
       sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"permission explained\"}\n\n"
                    "data: [DONE]\n\n")});
  ava::agent::AgentLoop denied_loop(ava::agent::AgentLoopOptions{.workspace_dir = denied_workspace,
                                                                 .mode = ava::agent::Mode::Plan,
                                                                 .provider_id = "openai",
                                                                 .model_id = "gpt-5.5",
                                                                 .system_prompt = "system prompt",
                                                                 .access_token = "token",
                                                                 .openai_oauth = true,
                                                                 .openai_account_id = "acct_123"});
  auto denied_result = denied_loop.run_turn("write source", denied_store, provider, denied_transport);
  expect(
      denied_result && denied_result->final_text == "permission explained" && denied_result->provider_iterations == 2,
      "agent loop continues after permission-denied tool results");
  expect(denied_result && denied_result->tool_timeline.size() == 1 &&
             denied_result->tool_timeline.front().status == ava::agent::ToolTimelineStatus::Error &&
             denied_result->tool_timeline.front().argument_summary.find("content=3 bytes") != std::string::npos &&
             denied_result->tool_timeline.front().argument_summary.find("bad") == std::string::npos &&
             denied_result->tool_timeline.front().result_summary.find("error:") == 0,
         "agent loop marks denied tool results as safe error timeline entries");
  expect(denied_transport.requests().size() == 2 &&
             denied_transport.requests()[1].body.find("permission_denied") != std::string::npos,
         "permission-denied tool result is framed into continuation context");
}

void test_agent_loop_tool_delta_dedupes_and_rejects_empty_tool_ids() {
  const ava::provider::OpenAIProvider provider("https://api.example.test");

  {
    const auto root = temp_root() / "agent-delta-before-start";
    std::error_code remove_error;
    std::filesystem::remove_all(root, remove_error);
    const auto workspace = root / "workspace";
    std::filesystem::create_directories(workspace);
    {
      std::ofstream file(workspace / "note.txt", std::ios::binary | std::ios::trunc);
      file << "dedupe content";
    }
    ava::session::SessionStore store(ava::session::SessionStoreOptions{
        .root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "delta-before-start"});
    ava::tests::FakeTransport transport(
        {sse_response(
             "data: "
             "{\"type\":\"response.function_call_arguments.delta\",\"item_id\":\"call_1\",\"delta\":\"{\\\"path\\\":"
             "\\\"note.txt\\\"}\"}\n\n"
             "data: {\"type\":\"response.function_call.added\",\"item_id\":\"call_1\",\"name\":\"read_file\"}\n\n"
             "data: {\"type\":\"response.function_call.done\",\"item_id\":\"call_1\"}\n\n"
             "data: [DONE]\n\n"),
         sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"done\"}\n\n"
                      "data: [DONE]\n\n")});
    ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{.workspace_dir = workspace,
                                                            .mode = ava::agent::Mode::Build,
                                                            .provider_id = "openai",
                                                            .model_id = "gpt-5.5",
                                                            .system_prompt = "system prompt",
                                                            .access_token = "token"});
    auto result = loop.run_turn("read note", store, provider, transport);
    expect(result && result->tool_calls == 1 && result->tool_timeline.size() == 1 &&
               result->tool_timeline.front().status == ava::agent::ToolTimelineStatus::Success &&
               result->tool_timeline.front().name == "read_file",
           "agent loop deduplicates tool deltas that arrive before tool start events");

    auto entries = store.load();
    std::size_t tool_calls = 0;
    std::size_t tool_results = 0;
    if (entries) {
      for (const auto& entry : *entries) {
        if (entry.type == ava::session::EntryType::ToolCall) ++tool_calls;
        if (entry.type == ava::session::EntryType::ToolResult) ++tool_results;
      }
    }
    expect(entries && tool_calls == 1 && tool_results == 1, "deduped streamed tool call has one paired result");
  }

  {
    const auto root = temp_root() / "agent-empty-call-id";
    std::error_code remove_error;
    std::filesystem::remove_all(root, remove_error);
    const auto workspace = root / "workspace";
    std::filesystem::create_directories(workspace);
    ava::session::SessionStore store(ava::session::SessionStoreOptions{
        .root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "empty-call-id"});
    ava::tests::FakeTransport transport(
        {sse_response("data: {\"type\":\"response.function_call.added\",\"item_id\":\"\",\"name\":\"read_file\"}\n\n"
                      "data: [DONE]\n\n")});
    ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{.workspace_dir = workspace,
                                                            .mode = ava::agent::Mode::Build,
                                                            .provider_id = "openai",
                                                            .model_id = "gpt-5.5",
                                                            .system_prompt = "system prompt",
                                                            .access_token = "token"});
    auto result = loop.run_turn("read missing-id", store, provider, transport);
    auto entries = store.load();
    bool saw_tool_entry = false;
    if (entries) {
      for (const auto& entry : *entries) {
        saw_tool_entry = saw_tool_entry || entry.type == ava::session::EntryType::ToolCall ||
                         entry.type == ava::session::EntryType::ToolResult;
      }
    }
    expect(!result && result.error().message().find("empty") != std::string::npos && entries && !saw_tool_entry,
           "agent loop rejects empty provider tool call ids before session or timeline use");
  }
}

void test_agent_loop_truncates_tool_context() {
  const auto root = temp_root() / "agent-tool-truncate";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  {
    std::ofstream large(workspace / "large.txt", std::ios::binary | std::ios::trunc);
    large << std::string(12 * 1024, 'x');
  }
  ava::session::SessionStore store(ava::session::SessionStoreOptions{
      .root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "truncate"});
  const ava::provider::OpenAIProvider provider("https://api.example.test");
  ava::tests::FakeTransport transport(
      {sse_response(
           "data: {\"type\":\"response.function_call.added\",\"item_id\":\"call_large\",\"name\":\"read_file\"}\n\n"
           "data: "
           "{\"type\":\"response.function_call_arguments.delta\",\"item_id\":\"call_large\",\"delta\":\"{\\\"path\\\":"
           "\\\"large.txt\\\"}\"}\n\n"
           "data: [DONE]\n\n"),
       sse_response("data: {\"type\":\"response.output_text.delta\",\"delta\":\"ok\"}\n\n"
                    "data: [DONE]\n\n")});
  ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{.workspace_dir = workspace,
                                                          .mode = ava::agent::Mode::Build,
                                                          .provider_id = "openai",
                                                          .model_id = "gpt-5.5",
                                                          .system_prompt = "system prompt",
                                                          .access_token = "token",
                                                          .max_tool_result_context_bytes = 8 * 1024});
  auto result = loop.run_turn("read large", store, provider, transport);
  expect(result && transport.requests().size() == 2 &&
             transport.requests()[1].body.find("tool result context truncated") != std::string::npos,
         "agent loop truncates tool results before provider continuation context");
}

void test_agent_loop_max_iteration_guard() {
  const auto root = temp_root() / "agent-max";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  const auto workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  ava::session::SessionStore store(ava::session::SessionStoreOptions{
      .root_dir = root / "sessions", .workspace_dir = workspace, .session_id = "max"});
  const std::string tool_sse =
      "data: {\"type\":\"response.function_call.added\",\"item_id\":\"call_glob\",\"name\":\"glob\"}\n\n"
      "data: "
      "{\"type\":\"response.function_call_arguments.delta\",\"item_id\":\"call_glob\",\"delta\":\"{\\\"pattern\\\":"
      "\\\"**/*\\\"}\"}\n\n"
      "data: {\"type\":\"response.function_call.done\",\"item_id\":\"call_glob\"}\n\n"
      "data: [DONE]\n\n";
  const ava::provider::OpenAIProvider provider("https://api.example.test");
  ava::tests::FakeTransport transport({sse_response(tool_sse), sse_response(tool_sse)});
  ava::agent::AgentLoop loop(ava::agent::AgentLoopOptions{.workspace_dir = workspace,
                                                          .mode = ava::agent::Mode::Build,
                                                          .provider_id = "openai",
                                                          .model_id = "gpt-5.5",
                                                          .system_prompt = "system prompt",
                                                          .access_token = "token",
                                                          .max_tool_iterations = 2});
  auto result = loop.run_turn("loop", store, provider, transport);
  expect(!result && result.error().message().find("maximum tool iterations") != std::string::npos,
         "agent loop stops repeated tool use at max iteration guard");
  auto entries = store.load();
  expect(entries && std::ranges::any_of(*entries,
                                        [](const ava::session::SessionEntry& entry) {
                                          return entry.type == ava::session::EntryType::Error &&
                                                 entry.data_json.find("maximum tool iterations") != std::string::npos;
                                        }),
         "agent loop persists max iteration guard as error event");
}

}  // namespace

void run_agent_tool_dispatcher_tests() {
  test_tool_dispatcher();
  test_tool_dispatcher_plan_mode_denies_mutation();
  test_agent_loop_text_only_turn();
  test_agent_loop_tool_turn_and_continuation();
  test_agent_loop_permission_resolver_threads_to_tools();
  test_agent_loop_question_resolver_threads_to_tools();
  test_agent_loop_non_stream_response();
  test_agent_loop_compaction_status_metadata();
  test_agent_loop_cancellation_boundaries();
  test_agent_loop_error_paths_and_bounds();
  test_agent_loop_multiple_tools_and_denied_continuation();
  test_agent_loop_tool_delta_dedupes_and_rejects_empty_tool_ids();
  test_agent_loop_truncates_tool_context();
  test_agent_loop_max_iteration_guard();
}
