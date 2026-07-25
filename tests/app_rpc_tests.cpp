#include "sys.h"
#include "tests/support/app_runtime_support.h"
#include "tests/support/fake_transport.h"
#include "tests/support/test_harness.h"
#include "ava/app/EventEnvelope.h"
#include "ava/app/events.h"
#include "ava/app/headless_policy.h"
#include "ava/app/project_trust.h"
#include "ava/app/rpc/catalog.h"
#include "ava/app/rpc/output.h"
#include "ava/app/rpc/resolvers.h"
#include "ava/app/rpc/run_state.h"
#include "ava/app/rpc/runtime_navigation.h"
#include "ava/app/rpc/serialization.h"
#include "ava/app/rpc/serialization_json.h"
#include "ava/app/rpc/session_operators.h"
#include "ava/app/rpc_mode.h"
#include "ava/app/runtime.h"
#include "ava/app/runtime/Event.h"
#include "ava/app/runtime/Session.h"
#include "ava/agent/question.h"
#include "ava/config/auth.h"
#include "ava/session/assistant_output.h"
#include "ava/session/attachments.h"
#include "ava/session/record.h"
#include "ava/session/session_metadata.h"
#include "ava/session/session_store.h"
#include "ava/session/validation.h"
#include "ava/permissions/permission.h"
#include "ava/provider/openai_provider.h"
#include "ava/provider/provider_utils.h"
#include "ava/core/ids.h"
#include "ava/core/json.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <optional>
#include <ranges>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>

#ifndef AVA_FAKE_MCP_SERVER_PATH
#define AVA_FAKE_MCP_SERVER_PATH ""
#endif

#ifndef AVA_RPC_V1_GOLDEN_DIR
#define AVA_RPC_V1_GOLDEN_DIR ""
#endif

namespace {

using namespace ava::tests;

class TerminalPublicationStreamBuf final : public std::streambuf
{
 public:
  explicit TerminalPublicationStreamBuf(std::string terminal_marker, bool fail_terminal = false)
      : terminal_marker_(std::move(terminal_marker)), fail_terminal_(fail_terminal)
  {
  }

  bool wait_until_terminal(std::chrono::milliseconds timeout)
  {
    std::unique_lock lock(mutex_);
    return cv_.wait_for(lock, timeout, [&] { return terminal_reached_; });
  }

  bool wait_contains(std::string_view value, std::chrono::milliseconds timeout)
  {
    std::unique_lock lock(mutex_);
    return cv_.wait_for(lock, timeout, [&] { return text_.find(value) != std::string::npos; });
  }

  bool wait_for_occurrences(std::string_view value, std::size_t count, std::chrono::milliseconds timeout)
  {
    std::unique_lock lock(mutex_);
    return cv_.wait_for(lock, timeout, [&] {
      std::size_t found = 0;
      std::size_t offset = 0;
      while ((offset = text_.find(value, offset)) != std::string::npos)
      {
        ++found;
        offset += value.size();
      }
      return found >= count;
    });
  }

  void release_terminal()
  {
    {
      std::lock_guard lock(mutex_);
      released_ = true;
    }
    cv_.notify_all();
  }

  std::string str() const
  {
    std::lock_guard lock(mutex_);
    return text_;
  }

 protected:
  int overflow(int ch) override
  {
    if (ch == traits_type::eof())
      return traits_type::not_eof(ch);
    {
      std::lock_guard lock(mutex_);
      text_.push_back(static_cast<char>(ch));
    }
    cv_.notify_all();
    return ch;
  }

  std::streamsize xsputn(char const* data, std::streamsize count) override
  {
    {
      std::lock_guard lock(mutex_);
      text_.append(data, static_cast<std::size_t>(count));
    }
    cv_.notify_all();
    return count;
  }

  int sync() override
  {
    std::unique_lock lock(mutex_);
    if (!terminal_handled_ && text_.find(terminal_marker_) != std::string::npos)
    {
      terminal_handled_ = true;
      terminal_reached_ = true;
      cv_.notify_all();
      if (fail_terminal_)
        return -1;
      cv_.wait(lock, [&] { return released_; });
    }
    return 0;
  }

 private:
  std::string terminal_marker_;
  bool fail_terminal_ = false;
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::string text_;
  bool terminal_handled_ = false;
  bool terminal_reached_ = false;
  bool released_ = false;
};

class WakeBadInputBuf final : public std::streambuf
{
 public:
  bool wait_until_blocked(std::chrono::milliseconds timeout)
  {
    std::unique_lock lock(mutex_);
    return cv_.wait_for(lock, timeout, [&] { return blocked_; });
  }

  void wake() noexcept
  {
    {
      std::lock_guard lock(mutex_);
      woken_ = true;
    }
    cv_.notify_all();
  }

 protected:
  int underflow() override
  {
    std::unique_lock lock(mutex_);
    blocked_ = true;
    cv_.notify_all();
    cv_.wait(lock, [&] { return woken_; });
    throw std::runtime_error("wake-induced stream failure");
  }

 private:
  std::mutex mutex_;
  std::condition_variable cv_;
  bool blocked_ = false;
  bool woken_ = false;
};

class PausingTerminalCallbackLineReader final : public ava::app::rpc::RpcLineReader
{
 public:
  explicit PausingTerminalCallbackLineReader(std::istream& input, ava::app::rpc::RpcInputWake wake) : input_(input, std::move(wake)) { }

  ava::core::Result<bool> read_line(std::string& line, ava::app::rpc::RpcInputTerminalCallback const& on_terminal) override
  {
    return input_.read_line(line, [this, &on_terminal](ava::app::rpc::RpcInputTerminalOutcome outcome) {
      if (on_terminal)
        on_terminal(outcome);
      {
        std::lock_guard lock(mutex_);
        terminal_callback_observed_ = true;
      }
      cv_.notify_all();
      std::unique_lock lock(mutex_);
      cv_.wait(lock, [&] { return terminal_callback_released_; });
    });
  }

  void cancel() noexcept override { input_.cancel(); }

  bool wait_until_terminal_callback(std::chrono::milliseconds timeout)
  {
    std::unique_lock lock(mutex_);
    return cv_.wait_for(lock, timeout, [&] { return terminal_callback_observed_; });
  }

  void release_terminal_callback()
  {
    {
      std::lock_guard lock(mutex_);
      terminal_callback_released_ = true;
    }
    cv_.notify_all();
  }

 private:
  ava::app::rpc::StreamRpcLineReader input_;
  std::mutex mutex_;
  std::condition_variable cv_;
  bool terminal_callback_observed_ = false;
  bool terminal_callback_released_ = false;
};

std::string read_rpc_golden(std::filesystem::path const& path)
{
  std::ifstream file(path, std::ios::binary);
  std::ostringstream contents;
  contents << file.rdbuf();
  return contents.str();
}

std::string rpc_tiny_png_bytes()
{
  std::string bytes;
  bytes.push_back(static_cast<char>(0x89));
  bytes += "PNG\r\n";
  bytes.push_back(static_cast<char>(0x1A));
  bytes += "\nava-rpc-image";
  return bytes;
}

std::optional<std::string> rpc_string_field_from_output(std::string_view jsonl, std::string_view field)
{
  auto const key = "\"" + std::string(field) + "\":\"";
  auto const start = jsonl.find(key);
  if (start == std::string_view::npos)
    return std::nullopt;
  auto value_start = start + key.size();
  auto const end = jsonl.find('"', value_start);
  if (end == std::string_view::npos)
    return std::nullopt;
  return std::string(jsonl.substr(value_start, end - value_start));
}

void test_app_rpc_prompt_payload_serialization()
{
  auto const permission_json =
      ava::app::rpc::permission_request_payload_json("permission_1", ava::permissions::PermissionPrompt{.permission_request_id = "permreq_1",
                                                                                                        .operation = ava::permissions::Operation::EditFile,
                                                                                                        .mode = ava::agent::Mode::Build,
                                                                                                        .workspace_dir = "/workspace",
                                                                                                        .target_path = "/workspace/src/main.cpp",
                                                                                                        .command = "",
                                                                                                        .tool_name = "edit_file",
                                                                                                        .reason = "needs approval",
                                                                                                        .risk = ava::permissions::PermissionRisk::High,
                                                                                                        .diff_preview = "--- a\n+++ b\n-old\n+new",
                                                                                                        .diff_truncated = true});
  expect(permission_json.find("\"operation\":\"edit\"") != std::string::npos &&
             permission_json.find("\"permission_request_id\":\"permreq_1\"") != std::string::npos &&
             permission_json.find("\"target_path\":\"/workspace/src/main.cpp\"") != std::string::npos &&
             permission_json.find("\"risk\":\"high\"") != std::string::npos &&
             permission_json.find("\"diff_preview\":\"--- a\\n+++ b\\n-old\\n+new\"") != std::string::npos &&
             permission_json.find("\"diff_truncated\":true") != std::string::npos,
         "RPC permission request payload preserves semantic operation, target, risk, reason, and diff preview data");

  // The additive optional command_metadata block carries the sealed command
  // plan identity so RPC clients can display exact execution context.
  ava::permissions::CommandPermissionMetadata command_metadata;
  command_metadata.level = ava::command::CommandLevel::Critical;
  command_metadata.family = ava::command::CommandFamily::InterpreterInline;
  command_metadata.fingerprint = "sha256:ava-command-plan-v3:test-fingerprint";
  command_metadata.execution_domain = ava::command::CommandExecutionDomain::RawShell;
  command_metadata.resolved_executable = "/usr/bin/env";
  command_metadata.executable_origin = ava::command::ExecutableOrigin::System;
  command_metadata.cwd = "/workspace";
  command_metadata.containment_available = false;
  command_metadata.containment_status = ava::permissions::CommandContainmentStatus::Unavailable;
  command_metadata.backend_maximum_scope = ava::command::InteractiveScope::Once;
  command_metadata.environment_profile_id = "ava-local-bash-prompt-v2";
  command_metadata.environment_digest = "abc123";
  command_metadata.executor_identity_verified = false;
  auto const command_permission_json =
      ava::app::rpc::permission_request_payload_json("permission_cmd", ava::permissions::PermissionPrompt{.permission_request_id = "permreq_cmd",
                                                                                                          .operation = ava::permissions::Operation::RunCommand,
                                                                                                          .mode = ava::agent::Mode::Build,
                                                                                                          .workspace_dir = "/workspace",
                                                                                                          .target_path = {},
                                                                                                          .command = "python3 -c 'print(1)'",
                                                                                                          .tool_name = "bash",
                                                                                                          .reason = "sealed command requires approval",
                                                                                                          .risk = ava::permissions::PermissionRisk::Critical,
                                                                                                          .command_metadata = command_metadata});
  expect(command_permission_json.find("\"command_metadata\":{") != std::string::npos &&
             command_permission_json.find("\"level\":\"critical\"") != std::string::npos &&
             command_permission_json.find("\"family\":\"interpreter_inline\"") != std::string::npos &&
             command_permission_json.find("\"fingerprint\":\"sha256:ava-command-plan-v3:test-fingerprint\"") != std::string::npos &&
             command_permission_json.find("\"execution_domain\":\"raw_shell\"") != std::string::npos &&
             command_permission_json.find("\"resolved_executable\":\"/usr/bin/env\"") != std::string::npos &&
             command_permission_json.find("\"origin\":\"system\"") != std::string::npos &&
             command_permission_json.find("\"cwd\":\"/workspace\"") != std::string::npos &&
             command_permission_json.find("\"containment_available\":false") != std::string::npos &&
             command_permission_json.find("\"containment_status\":\"unavailable\"") != std::string::npos &&
             command_permission_json.find("\"backend_maximum_scope\":\"once\"") != std::string::npos &&
             command_permission_json.find("\"environment_profile_id\":\"ava-local-bash-prompt-v2\"") != std::string::npos &&
             command_permission_json.find("\"environment_digest\":\"abc123\"") != std::string::npos &&
             command_permission_json.find("\"executor_identity_verified\":false") != std::string::npos,
         "RPC permission request payload includes additive optional command_metadata fields for sealed commands");

  // The command_recipe_key is additive in session grant serialization: it is
  // omitted when empty and present when a sealed command plan is bound.
  ava::app::rpc::PendingResolverState grant_pending_state;
  grant_pending_state.permission_session_grants.push_back(ava::app::rpc::PermissionSessionGrant{.grant_id = "permgrant_1",
                                                                                                .permission_request_id = "permreq_1",
                                                                                                .session_id = "session_1",
                                                                                                .operation = ava::permissions::Operation::RunCommand,
                                                                                                .mode = ava::agent::Mode::Build,
                                                                                                .tool_name = "bash",
                                                                                                .target_path = {},
                                                                                                .command = "true",
                                                                                                .command_recipe_key = {},
                                                                                                .command_recipe_display = {},
                                                                                                .reason = "one-shot",
                                                                                                .risk = ava::permissions::PermissionRisk::Critical});
  auto const grants_json_no_recipe = ava::app::rpc::permission_session_grants_result_json(grant_pending_state);
  expect(grants_json_no_recipe.find("\"command_recipe_key\"") == std::string::npos,
         "RPC session grant serialization omits command_recipe_key when no sealed plan is bound");
  grant_pending_state.permission_session_grants.front().command_recipe_key =
      "sha256:ava-command-workspace-recipe-v1:dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd";
  grant_pending_state.permission_session_grants.front().command_recipe_display = "ctest";
  auto const grants_json_with_recipe = ava::app::rpc::permission_session_grants_result_json(grant_pending_state);
  expect(grants_json_with_recipe.find(
             "\"command_recipe_key\":\"sha256:ava-command-workspace-recipe-v1:dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd\"") !=
             std::string::npos,
         "RPC session grant serialization includes command_recipe_key when a sealed plan is bound");

  auto const question_json = ava::app::rpc::question_request_payload_json(
      "question_1", ava::agent::QuestionPrompt{.header = "Choose",
                                               .question = "Pick providers",
                                               .options = {ava::agent::QuestionOption{.value = "openai", .label = "OpenAI"}},
                                               .multiple = true,
                                               .allow_custom = true,
                                               .secret = true,
                                               .modal = true,
                                               .searchable = true});
  expect(question_json.find("\"options\":[{\"value\":\"openai\",\"label\":\"OpenAI\"}]") != std::string::npos &&
             question_json.find("\"multiple\":true") != std::string::npos && question_json.find("\"allow_custom\":true") != std::string::npos &&
             question_json.find("\"secret\":true") != std::string::npos && question_json.find("\"modal\":true") != std::string::npos &&
             question_json.find("\"searchable\":true") != std::string::npos,
         "RPC question request payload preserves options, selection metadata, and local prompt flags");

  auto const permission_reply_json = ava::app::rpc::permission_reply_payload_json("permission_1", "deny", std::optional<std::string>{"not approved"});
  expect(permission_reply_json.find("\"resolver_request_id\":\"permission_1\"") != std::string::npos &&
             permission_reply_json.find("\"decision\":\"deny\"") != std::string::npos &&
             permission_reply_json.find("\"reason\":\"not approved\"") != std::string::npos,
         "RPC permission reply payload preserves client-supplied resolution reasons");

  auto const question_reply_json = ava::app::rpc::question_reply_payload_json(
      "question_1", std::optional<std::string>{"custom"}, std::nullopt, std::optional<std::vector<std::string>>{std::vector<std::string>{"alpha", "beta"}});
  expect(question_reply_json.find("\"resolver_request_id\":\"question_1\"") != std::string::npos &&
             question_reply_json.find("\"answer\":\"custom\"") != std::string::npos &&
             question_reply_json.find("\"selected_options\":[\"alpha\",\"beta\"]") != std::string::npos,
         "RPC question reply payload preserves multiple selections and custom text");

  ava::agent::ToolTimelineEntry tool_entry;
  tool_entry.status = ava::agent::ToolTimelineStatus::Success;
  tool_entry.call_id = "call_prompt";
  tool_entry.name = "read_file";
  tool_entry.result_summary = "read ok";
  tool_entry.result_json = "{\"ok\":true,\"path\":\"README.md\"}";
  tool_entry.structured_result_json =
      "{\"schema_version\":1,\"call_id\":\"call_prompt\",\"tool\":\"read_file\","
      "\"status\":\"success\",\"ok\":true,\"content_type\":\"application/json\","
      "\"content\":{\"ok\":true,\"path\":\"README.md\"}}";
  tool_entry.content_type = "application/json";
  tool_entry.output_lines = 1;
  ava::agent::AgentLoopResult prompt_result;
  prompt_result.final_text = "done";
  prompt_result.outcome = ava::core::RuntimeTerminalOutcome::Completed;
  prompt_result.tool_calls = 1;
  prompt_result.tool_timeline.push_back(tool_entry);
  auto const prompt_json = ava::app::rpc::prompt_result_json("session_1", prompt_result);
  expect(prompt_json.find("\"tool_timeline\"") != std::string::npos && prompt_json.find("\"structured_result\":{\"schema_version\":1") != std::string::npos &&
             prompt_json.find("\"tool\":\"read_file\"") != std::string::npos && prompt_json.find("\"output_lines\":1") != std::string::npos,
         "RPC prompt results expose structured tool timeline metadata");

  auto const cancel_json = ava::app::rpc::cancel_requested_payload_json(true, 2, 1, "prompt_1");
  expect(cancel_json == "{\"active_run\":true,\"cleared_steer\":2,\"cleared_follow_up\":1,\"active_request_id\":\"prompt_1\"}",
         "RPC cancel request payload preserves active-run and cleared-queue counters");
}

void test_app_rpc_prompt_result_tool_timeline_golden_payloads()
{
  std::string const success_structured =
      "{\"schema_version\":1,\"call_id\":\"call_success\",\"tool\":\"write_file\",\"status\":\"success\","
      "\"ok\":true,\"content_type\":\"application/json\",\"content\":{\"ok\":true,\"path\":\"note.txt\"},"
      "\"changed_paths\":[\"note.txt\"],\"permission_request_ids\":[\"permreq_write\"]}";
  ava::agent::ToolTimelineEntry success_entry;
  success_entry.status = ava::agent::ToolTimelineStatus::Success;
  success_entry.call_id = "call_success";
  success_entry.name = "write_file";
  success_entry.result_summary = "wrote note";
  success_entry.arguments_json = "{\"path\":\"note.txt\"}";
  success_entry.result_json = "{\"ok\":true,\"path\":\"note.txt\"}";
  success_entry.structured_result_json = success_structured;
  success_entry.content_type = "application/json";
  success_entry.diff = "--- note.txt\n+++ note.txt\n-old\n+new";
  success_entry.changed_paths = {"note.txt"};
  success_entry.permission_request_ids = {"permreq_write"};
  success_entry.spill_path = "spill/tool.txt";
  success_entry.diff_truncated = true;
  success_entry.truncated = true;
  success_entry.byte_limited = true;
  success_entry.line_limited = true;
  success_entry.spill_truncated = true;
  success_entry.output_bytes = 128;
  success_entry.total_bytes = 512;
  success_entry.output_lines = 2;
  success_entry.total_lines = 5;
  success_entry.start_line = 1;
  success_entry.end_line = 2;
  success_entry.next_offset_line = 3;
  success_entry.omitted_bytes = 384;
  success_entry.omitted_lines = 3;

  std::string const denied_structured =
      "{\"schema_version\":1,\"call_id\":\"call_denied\",\"tool\":\"write_file\",\"status\":\"error\","
      "\"ok\":false,\"summary\":\"permission denied\",\"content_type\":\"text/plain\","
      "\"content\":\"permission denied\",\"error\":{\"category\":\"permission\",\"code\":\"permission_denied\","
      "\"message\":\"permission denied\",\"details\":\"resolution: deny\"}}";
  ava::agent::ToolTimelineEntry denied_entry;
  denied_entry.status = ava::agent::ToolTimelineStatus::Error;
  denied_entry.call_id = "call_denied";
  denied_entry.name = "write_file";
  denied_entry.result_summary = "permission denied";
  denied_entry.structured_result_json = denied_structured;
  denied_entry.content_type = "text/plain";
  denied_entry.error_category = "permission";
  denied_entry.error_code = "permission_denied";
  denied_entry.error_message = "permission denied";
  denied_entry.error_details = "resolution: deny";

  std::string const canceled_structured =
      "{\"schema_version\":1,\"call_id\":\"call_canceled\",\"tool\":\"glob\",\"status\":\"canceled\","
      "\"ok\":false,\"summary\":\"tool canceled\",\"content_type\":\"text/plain\","
      "\"content\":\"tool canceled\",\"error\":{\"category\":\"canceled\",\"code\":\"tool_canceled\","
      "\"message\":\"tool call canceled\",\"details\":\"cancel requested\"}}";
  ava::agent::ToolTimelineEntry canceled_entry;
  canceled_entry.status = ava::agent::ToolTimelineStatus::Canceled;
  canceled_entry.call_id = "call_canceled";
  canceled_entry.name = "glob";
  canceled_entry.result_summary = "tool canceled";
  canceled_entry.structured_result_json = canceled_structured;
  canceled_entry.content_type = "text/plain";
  canceled_entry.error_category = "canceled";
  canceled_entry.error_code = "tool_canceled";
  canceled_entry.error_message = "tool call canceled";
  canceled_entry.error_details = "cancel requested";

  ava::agent::AgentLoopResult prompt_result;
  prompt_result.final_text = "done";
  prompt_result.outcome = ava::core::RuntimeTerminalOutcome::Completed;
  prompt_result.provider_iterations = 2;
  prompt_result.tool_calls = 3;
  prompt_result.tool_timeline = {success_entry, denied_entry, canceled_entry};

  auto const success_entry_json = std::string{"{\"status\":\"success\",\"call_id\":\"call_success\",\"tool\":\"write_file\","} +
                                  "\"text\":\"wrote note\",\"result_summary\":\"wrote note\",\"args\":{\"path\":\"note.txt\"}," +
                                  "\"result\":{\"ok\":true,\"path\":\"note.txt\"},\"structured_result\":" + success_structured +
                                  ",\"content_type\":\"application/json\",\"diff\":\"--- note.txt\\n+++ note.txt\\n-old\\n+new\"," +
                                  "\"changed_paths\":[\"note.txt\"],\"permission_request_ids\":[\"permreq_write\"]," +
                                  "\"spill_path\":\"spill/tool.txt\",\"diff_truncated\":true,\"truncated\":true,"
                                  "\"byte_limited\":true,\"line_limited\":true,\"spill_truncated\":true,\"output_bytes\":128,"
                                  "\"total_bytes\":512,\"output_lines\":2,\"total_lines\":5,\"start_line\":1,\"end_line\":2,"
                                  "\"next_offset_line\":3,\"omitted_bytes\":384,\"omitted_lines\":3}";
  auto const denied_entry_json = std::string{"{\"status\":\"error\",\"call_id\":\"call_denied\",\"tool\":\"write_file\","} +
                                 "\"text\":\"permission denied\",\"result_summary\":\"permission denied\",\"structured_result\":" + denied_structured +
                                 ",\"content_type\":\"text/plain\",\"category\":\"permission\",\"error_code\":\"permission_denied\"," +
                                 "\"message\":\"permission denied\",\"details\":\"resolution: deny\"}";
  auto const canceled_entry_json = std::string{"{\"status\":\"canceled\",\"call_id\":\"call_canceled\",\"tool\":\"glob\","} +
                                   "\"text\":\"tool canceled\",\"result_summary\":\"tool canceled\",\"structured_result\":" + canceled_structured +
                                   ",\"content_type\":\"text/plain\",\"category\":\"canceled\",\"error_code\":\"tool_canceled\"," +
                                   "\"message\":\"tool call canceled\",\"details\":\"cancel requested\"}";
  auto const expected_json = std::string{"{\"session_id\":\"session_rpc\",\"final_text\":\"done\",\"stop_reason\":\"completed\","} +
                             "\"provider_iterations\":2,\"tool_calls\":3,\"tool_timeline\":[" + success_entry_json + "," + denied_entry_json + "," +
                             canceled_entry_json + "]}";

  auto const prompt_json = ava::app::rpc::prompt_result_json("session_rpc", prompt_result);
  expect(prompt_json == expected_json,
         "RPC prompt result golden locks success, denied/error, canceled, diff, changed path, permission id, and spill tool timeline shapes");
}

void test_app_rpc_parsing_and_response_serialization()
{
  auto command = ava::app::parse_rpc_command_line("{\"id\":\"1\",\"type\":\"prompt\",\"message\":\"hello\\nava\",\"instructions\":\"keep\"}");
  expect(command && command->id == "1" && command->type == "prompt" && command->message && *command->message == "hello\nava" && command->instructions &&
             *command->instructions == "keep",
         "RPC parser extracts string envelope fields and unescapes JSON strings");

  auto image_prompt = ava::app::parse_rpc_command_line(R"JSON({"id":"img","type":"prompt","message":"look","attachments":["screen.png"]})JSON");
  expect(image_prompt && image_prompt->attachments && image_prompt->attachments->size() == 1 && (*image_prompt->attachments)[0] == "screen.png",
         "RPC parser preserves prompt image attachment path arrays");

  auto uploaded_image_prompt = ava::app::parse_rpc_command_line(
      R"JSON({"id":"img-upload","type":"prompt","message":"look","images":[{"type":"image","data":"QUJD","mimeType":"image/png"}]})JSON");
  expect(uploaded_image_prompt && uploaded_image_prompt->images && uploaded_image_prompt->images->size() == 1 &&
             (*uploaded_image_prompt->images)[0].type == "image" && (*uploaded_image_prompt->images)[0].data_base64 == "QUJD" &&
             (*uploaded_image_prompt->images)[0].mime_type == "image/png",
         "RPC parser preserves Pi-style inline image upload objects");

  auto invalid_uploaded_image = ava::app::parse_rpc_command_line(R"JSON({"id":"img-upload-bad","type":"prompt","message":"look","images":["bad"]})JSON");
  expect(!invalid_uploaded_image && invalid_uploaded_image.error().message() == "RPC images must be an array of image objects",
         "RPC parser rejects non-object inline image upload entries");

  auto missing_upload_data = ava::app::parse_rpc_command_line(
      R"JSON({"id":"img-upload-missing","type":"prompt","message":"look","images":[{"type":"image","mimeType":"image/png"}]})JSON");
  expect(!missing_upload_data && missing_upload_data.error().message() == "RPC images entries require data",
         "RPC parser rejects inline image uploads without data");

  auto invalid_upload_type = ava::app::parse_rpc_command_line(
      R"JSON({"id":"img-upload-type","type":"prompt","message":"look","images":[{"type":"file","data":"QUJD","mimeType":"image/png"}]})JSON");
  expect(!invalid_upload_type && invalid_upload_type.error().message() == "RPC images entries must have type image",
         "RPC parser rejects inline image upload entries with non-image type");

  auto invalid_image_prompt = ava::app::parse_rpc_command_line(R"JSON({"id":"img-bad","type":"prompt","message":"look","attachments":["ok.png",2]})JSON");
  expect(!invalid_image_prompt && invalid_image_prompt.error().message() == "RPC attachments must be an array of strings",
         "RPC parser rejects non-string prompt attachment path entries");

  auto empty_image_prompt = ava::app::parse_rpc_command_line(R"JSON({"id":"img-empty","type":"prompt","message":"look","attachments":[""]})JSON");
  expect(!empty_image_prompt && empty_image_prompt.error().message() == "RPC attachments entries must be non-empty",
         "RPC parser rejects empty prompt attachment paths");

  auto reply = ava::app::parse_rpc_command_line(R"JSON({"id":"reply","type":"permission_reply","request_id":"permission_1",)JSON"
                                                R"JSON("correlation_id":"prompt_1","decision":"deny","reason":"not approved for this run"})JSON");
  expect(reply && reply->reason && *reply->reason == "not approved for this run", "RPC parser preserves optional permission reply reasons");

  auto question_reply = ava::app::parse_rpc_command_line(R"JSON({"id":"question","type":"question_reply","request_id":"question_1",)JSON"
                                                         R"JSON("correlation_id":"prompt_1","selected_options":["alpha","beta"],"answer":"custom"})JSON");
  expect(question_reply && question_reply->selected_options && question_reply->selected_options->size() == 2 &&
             (*question_reply->selected_options)[0] == "alpha" && (*question_reply->selected_options)[1] == "beta",
         "RPC parser preserves selected_options arrays for question replies");

  auto branch = ava::app::parse_rpc_command_line(R"JSON({"id":"fork","type":"fork_session","branch_from_entry_id":"entry_1"})JSON");
  expect(branch && branch->branch_from_entry_id && *branch->branch_from_entry_id == "entry_1",
         "RPC parser preserves branch_from_entry_id for session fork commands");

  auto empty_branch = ava::app::parse_rpc_command_line(R"JSON({"id":"fork","type":"fork_session","branch_from_entry_id":""})JSON");
  expect(!empty_branch && empty_branch.error().message() == "fork_session branch_from_entry_id must be non-empty when provided",
         "RPC parser rejects empty explicit branch_from_entry_id for session fork commands");

  auto non_string_session = ava::app::parse_rpc_command_line(R"JSON({"id":"summary","type":"summarize_branch","session_id":123})JSON");
  expect(!non_string_session && non_string_session.error().message() == "RPC session_id must be a string",
         "RPC parser rejects non-string session selectors for session mutation commands");

  auto non_string_session_name = ava::app::parse_rpc_command_line(R"JSON({"id":"fork","type":"fork_session","session_name":123})JSON");
  expect(!non_string_session_name && non_string_session_name.error().message() == "RPC session_name must be a string",
         "RPC parser rejects non-string session names for session mutation commands");

  auto branch_summary = ava::app::parse_rpc_command_line(R"JSON({"id":"summary","type":"summarize_branch","branch_root_entry_id":"entry_1",)JSON"
                                                         R"JSON("branch_tip_entry_id":"entry_2","summary":"line one\nline two",)JSON"
                                                         R"JSON("provider":"openai","model":"gpt-test","reason":"user requested"})JSON");
  expect(branch_summary && branch_summary->branch_root_entry_id && *branch_summary->branch_root_entry_id == "entry_1" && branch_summary->branch_tip_entry_id &&
             *branch_summary->branch_tip_entry_id == "entry_2" && branch_summary->summary && *branch_summary->summary == "line one\nline two" &&
             branch_summary->provider && *branch_summary->provider == "openai" && branch_summary->model && *branch_summary->model == "gpt-test" &&
             branch_summary->reason && *branch_summary->reason == "user requested",
         "RPC parser preserves summarize_branch source range, summary text, and provenance fields");

  auto invalid_branch_summary = ava::app::parse_rpc_command_line(R"JSON({"id":"summary","type":"summarize_branch","summary":"bad\u001b"})JSON");
  expect(!invalid_branch_summary && invalid_branch_summary.error().message() == "RPC text field contains invalid character",
         "RPC parser rejects control bytes in branch summary text");

  auto plugin_command_object_args = ava::app::parse_rpc_command_line(
      R"JSON({"id":"plugin","type":"run_plugin_command","plugin_id":"com.example.tool","name":"status","arguments":{"verbose":true}})JSON");
  expect(plugin_command_object_args && plugin_command_object_args->arguments && *plugin_command_object_args->arguments == R"JSON({"verbose":true})JSON",
         "RPC parser preserves object-shaped plugin command arguments");

  auto plugin_command_string_args = ava::app::parse_rpc_command_line(
      R"JSON({"id":"plugin","type":"run_plugin_command","plugin_id":"com.example.tool","name":"status","arguments":"{\"verbose\":true}"})JSON");
  expect(plugin_command_string_args && plugin_command_string_args->arguments && *plugin_command_string_args->arguments == R"JSON({"verbose":true})JSON",
         "RPC parser accepts legacy stringified JSON-object plugin command arguments");

  auto invalid_plugin_command_args = ava::app::parse_rpc_command_line(
      R"JSON({"id":"plugin","type":"run_plugin_command","plugin_id":"com.example.tool","name":"status","arguments":"not json"})JSON");
  expect(!invalid_plugin_command_args && invalid_plugin_command_args.error().message() == "RPC string arguments must contain a JSON object",
         "RPC parser rejects string plugin command arguments that are not JSON objects");

  auto non_object_plugin_command_args =
      ava::app::parse_rpc_command_line(R"JSON({"id":"plugin","type":"run_plugin_command","plugin_id":"com.example.tool","name":"status","arguments":123})JSON");
  expect(!non_object_plugin_command_args && non_object_plugin_command_args.error().message() == "RPC arguments must be an object",
         "RPC parser rejects non-object plugin command arguments instead of silently defaulting");

  auto wait_job = ava::app::parse_rpc_command_line(R"JSON({"id":"wait-job","type":"wait_job","job_id":"job_123","timeout_ms":45000})JSON");
  expect(wait_job && wait_job->job_id == "job_123" && wait_job->timeout_ms == 45000, "RPC parser preserves strict job wait fields for handler clamping");
  auto invalid_job_id = ava::app::parse_rpc_command_line(R"JSON({"id":"job","type":"get_job","job_id":42})JSON");
  expect(!invalid_job_id && invalid_job_id.error().message() == "RPC job_id must be a string", "RPC parser rejects wrong-typed job identifiers");
  auto invalid_job_timeout = ava::app::parse_rpc_command_line(R"JSON({"id":"job","type":"wait_job","job_id":"job_123","timeout_ms":"1"})JSON");
  expect(!invalid_job_timeout && invalid_job_timeout.error().message() == "RPC timeout_ms must be an integer",
         "RPC parser rejects wrong-typed job wait timeouts");

  auto direct_run_command = ava::app::parse_rpc_command_line(R"JSON({"id":"cmd","type":"run_command","command":"printf rpc-direct"})JSON");
  expect(direct_run_command && direct_run_command->command && *direct_run_command->command == "printf rpc-direct",
         "RPC parser preserves direct command execution payloads");

  auto invalid_direct_run_command = ava::app::parse_rpc_command_line(R"JSON({"id":"cmd","type":"run_command","command":123})JSON");
  expect(!invalid_direct_run_command && invalid_direct_run_command.error().message() == "RPC command must be a string",
         "RPC parser rejects non-string direct command execution payloads");

  auto malformed_with_string_prefix = ava::app::parse_rpc_command_line(R"JSON({"id":"bad","type":"summarize_branch","session_id":"session_a" garbage})JSON");
  expect(!malformed_with_string_prefix && malformed_with_string_prefix.error().message() == "malformed RPC JSON object",
         "RPC parser rejects malformed JSON even when a string prefix is parseable");

  auto clone_with_branch_from = ava::app::parse_rpc_command_line(R"JSON({"id":"clone","type":"clone_session","branch_from_entry_id":"entry_1"})JSON");
  expect(!clone_with_branch_from && clone_with_branch_from.error().message() == "clone_session does not support branch_from_entry_id",
         "RPC parser rejects misleading branch_from_entry_id on clone_session");

  auto unrelated_with_command_specific_fields = ava::app::parse_rpc_command_line(
      "{\"id\":\"prompt\",\"type\":\"prompt\",\"message\":\"hello\","
      "\"labels\":[\"ok\",2],\"branch_from_entry_id\":\"bad\\u001b\",\"grant_id\":\"bad grant\"}");
  expect(unrelated_with_command_specific_fields && unrelated_with_command_specific_fields->type == "prompt",
         "RPC parser ignores command-specific session and grant fields on unrelated commands");

  auto invalid_grant_revoke = ava::app::parse_rpc_command_line(R"JSON({"id":"revoke","type":"permission_grant_revoke","grant_id":"bad grant"})JSON");
  expect(!invalid_grant_revoke && invalid_grant_revoke.error().message() == "RPC identifier contains invalid character",
         "RPC parser validates grant_id only for permission_grant_revoke");

  auto invalid_selected_options = ava::app::parse_rpc_command_line(R"JSON({"id":"bad","type":"question_reply","selected_options":["ok",2]})JSON");
  expect(!invalid_selected_options && invalid_selected_options.error().message() == "RPC selected_options must be an array of strings",
         "RPC parser rejects non-string selected_options entries");

  auto oversized_reason = ava::app::parse_rpc_command_line("{\"id\":\"reply\",\"type\":\"permission_reply\",\"reason\":\"" +
                                                           std::string(ava::app::rpc::kMaxRpcReasonBytes + 1, 'x') + "\"}");
  expect(!oversized_reason && oversized_reason.error().message() == "RPC text field is too long",
         "RPC parser rejects oversized text fields before emitting resolver events");

  auto control_reason = ava::app::parse_rpc_command_line(R"JSON({"id":"reply","type":"permission_reply","reason":"bad\u001b"})JSON");
  expect(!control_reason && control_reason.error().message() == "RPC text field contains invalid character",
         "RPC parser rejects control bytes in free-text resolver reasons");

  auto malformed = ava::app::parse_rpc_command_line("{\"id\":\"bad\",\"type\":\"prompt\"");
  expect(!malformed && malformed.error().category() == ava::core::ErrorCategory::InvalidArgument, "RPC parser rejects malformed JSON object lines");

  auto oversized_id = ava::app::parse_rpc_command_line("{\"id\":\"" + std::string(257, 'x') + "\",\"type\":\"prompt\"}");
  expect(!oversized_id && oversized_id.error().message() == "RPC identifier is too long", "RPC parser rejects oversized request identifiers before queueing");

  auto const success = ava::app::serialize_rpc_success_jsonl("a\"b", "{\"value\":1}");
  expect(success == "{\"id\":\"a\\\"b\",\"type\":\"response\",\"success\":true,\"result\":{\"value\":1}}\n",
         "RPC success response serializes deterministic JSONL with escaped id");

  auto const error = ava::app::serialize_rpc_error_jsonl("e1", ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "bad \"request\""));
  expect(error.find("\"success\":false") != std::string::npos && error.find("bad \\\"request\\\"") != std::string::npos && error.ends_with('\n'),
         "RPC error response serializes JSONL error details");
}

void test_app_rpc_identifier_validation()
{
  auto allowed = ava::app::parse_rpc_command_line(R"JSON({"id":"rpc.1","type":"set_model","request_id":"req_1","correlation_id":"corr-1",)JSON"
                                                  R"JSON("provider":"openai","model":"openai/gpt-5.5","plugin_id":"com.example.rpc",)JSON"
                                                  R"JSON("name":"demo-server_1","server_id":"demo-server_1"})JSON");
  expect(allowed && allowed->model && *allowed->model == "openai/gpt-5.5" && allowed->plugin_id && *allowed->plugin_id == "com.example.rpc" &&
             allowed->server_id && *allowed->server_id == "demo-server_1",
         "RPC parser allows practical dotted, dashed, underscored, and slash-delimited identifiers");

  auto control = ava::app::parse_rpc_command_line(R"JSON({"id":"bad\u001f","type":"prompt"})JSON");
  expect(!control && control.error().message() == "RPC identifier contains invalid character", "RPC parser rejects escaped control bytes in identifiers");

  std::string del_line = R"JSON({"id":"bad)JSON";
  del_line.push_back(static_cast<char>(0x7F));
  del_line += R"JSON(","type":"prompt"})JSON";
  auto del = ava::app::parse_rpc_command_line(del_line);
  expect(!del && del.error().message() == "RPC identifier contains invalid character", "RPC parser rejects DEL bytes in identifiers");

  auto whitespace = ava::app::parse_rpc_command_line(R"JSON({"id":"bad id","type":"prompt"})JSON");
  expect(!whitespace && whitespace.error().message() == "RPC identifier contains invalid character", "RPC parser rejects ASCII whitespace in identifiers");

  auto metachar = ava::app::parse_rpc_command_line(R"JSON({"id":"ok","type":"inspect_plugin","plugin_id":"bad;id"})JSON");
  expect(!metachar && metachar.error().message() == "RPC identifier contains invalid character",
         "RPC parser rejects command-ambiguous metacharacters in slash-command identifiers");

  auto path = ava::app::parse_rpc_command_line(R"JSON({"id":"path-ok","type":"validate_plugin","path":"./plugins/bad; path.json"})JSON");
  expect(path && path->path && *path->path == "./plugins/bad; path.json", "RPC parser leaves validate_plugin path validation to the plugin path handler");

  std::string escaped_control_path = R"JSON({"id":"path-bad","type":"install_plugin","path":"./plugins/)JSON";
  escaped_control_path += "\\u001f";
  escaped_control_path += R"JSON(bad"})JSON";
  auto control_path = ava::app::parse_rpc_command_line(escaped_control_path);
  expect(!control_path && control_path.error().message() == "RPC text field contains invalid character", "RPC parser rejects control bytes in path fields");
}

void test_app_rpc_prompt_with_fake_transport_streams_events()
{
  auto const root = create_empty_root("app-rpc-prompt");

  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::runtime::OpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.mode = ava::agent::Mode::Build;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC prompt test opens runtime session");
  if (!session)
    return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({ava::provider::HttpResponse{
      .status_code = 200,
      .headers = {},
      .body = "data: {\"type\":\"response.output_text.delta\",\"delta\":\"rpc answer\"}\n\n"
              "data: [DONE]\n\n",
  }});
  ava::app::runtime::RunOptions runtime_options;
  runtime_options.access_token = "token";
  BlockingInputBuf input_buffer;
  std::istream in(&input_buffer);
  ThreadSafeStringBuf output_buffer;
  std::ostream out(&output_buffer);
  ava::core::VoidResult result;
  ava::app::runtime::session_ts unlocked_session(std::move(*session));
  std::jthread rpc_thread([&] {
    result = ava::app::run_rpc_loop(unlocked_session, open_options, provider, transport, runtime_options, in, out, [&] noexcept { input_buffer.close(); });
  });
  input_buffer.push("{\"id\":\"p1\",\"type\":\"prompt\",\"message\":\"hello rpc\"}\n");
  bool const completed = output_buffer.wait_contains("rpc answer", std::chrono::seconds(2));
  input_buffer.close();
  rpc_thread.join();
  auto const jsonl = output_buffer.str();
  expect(result.has_value(), "RPC prompt loop completes successfully");
  expect(transport.requests().size() == 1 && transport.requests()[0].body.find("hello rpc") != std::string::npos,
         "RPC prompt sends command message through shared runtime");
  expect(jsonl.find("\"name\":\"session_start\"") != std::string::npos && jsonl.find("\"name\":\"assistant_message\"") != std::string::npos &&
             jsonl.find("\"request_id\":\"p1\"") != std::string::npos && completed && jsonl.find("\"id\":\"p1\"") != std::string::npos &&
             jsonl.find("\"success\":true") != std::string::npos && jsonl.find("rpc answer") != std::string::npos,
         "RPC prompt streams runtime event envelopes and ends with a successful response");
}

void test_app_rpc_offline_allows_local_protocol_and_rejects_prompt_before_provider_request()
{
  auto const root = create_empty_root("app-rpc-offline");

  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::runtime::OpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.mode = ava::agent::Mode::Build;
  open_options.paths = paths;
  open_options.offline = true;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC offline test opens runtime session");
  if (!session)
    return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({});
  ava::app::runtime::RunOptions runtime_options;
  runtime_options.offline = true;
  BlockingInputBuf input_buffer;
  std::istream in(&input_buffer);
  ThreadSafeStringBuf output_buffer;
  std::ostream out(&output_buffer);
  ava::core::VoidResult result;
  ava::app::runtime::session_ts unlocked_session(std::move(*session));
  std::jthread rpc_thread([&] {
    result = ava::app::run_rpc_loop(unlocked_session, open_options, provider, transport, runtime_options, in, out, [&] noexcept { input_buffer.close(); });
  });
  input_buffer.push("{\"id\":\"proto\",\"type\":\"get_protocol\"}\n");
  bool const protocol_completed = output_buffer.wait_contains("\"id\":\"proto\"", std::chrono::seconds(2));
  input_buffer.push("{\"id\":\"p-offline\",\"type\":\"prompt\",\"message\":\"hello offline rpc\"}\n");
  bool const prompt_failed = output_buffer.wait_contains("offline mode is enabled", std::chrono::seconds(2));
  input_buffer.close();
  rpc_thread.join();

  auto const jsonl = output_buffer.str();
  expect(result.has_value(), "RPC offline loop completes successfully after prompt rejection");
  expect(protocol_completed && jsonl.find("\"id\":\"proto\"") != std::string::npos && jsonl.find("\"success\":true") != std::string::npos,
         "RPC offline mode still serves local protocol commands");
  expect(prompt_failed && jsonl.find("\"id\":\"p-offline\"") != std::string::npos && jsonl.find("\"success\":false") != std::string::npos,
         "RPC offline mode rejects prompt commands with a machine-readable error");
  expect(transport.requests().empty(), "RPC offline prompt rejection avoids provider transport requests");
}

void test_app_rpc_prompt_imports_image_attachments()
{
  auto const root = create_empty_root("app-rpc-prompt-image-attachment");

  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  auto const image_path = workspace / "screen.png";
  write_app_test_file(image_path, rpc_tiny_png_bytes());

  ava::app::runtime::OpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.mode = ava::agent::Mode::Build;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC image prompt test opens runtime session");
  if (!session)
    return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({ava::provider::HttpResponse{
      .status_code = 200,
      .headers = {},
      .body = "data: {\"type\":\"response.output_text.delta\",\"delta\":\"rpc image answer\"}\n\n"
              "data: [DONE]\n\n",
  }});
  ava::app::runtime::RunOptions runtime_options;
  runtime_options.access_token = "token";
  BlockingInputBuf input_buffer;
  std::istream in(&input_buffer);
  ThreadSafeStringBuf output_buffer;
  std::ostream out(&output_buffer);
  ava::core::VoidResult result;
  ava::app::runtime::session_ts unlocked_session(std::move(*session));
  std::jthread rpc_thread([&] {
    result = ava::app::run_rpc_loop(unlocked_session, open_options, provider, transport, runtime_options, in, out, [&] noexcept { input_buffer.close(); });
  });
  input_buffer.push("{\"id\":\"p-img\",\"type\":\"prompt\",\"message\":\"describe\",\"attachments\":[\"" + ava::core::json::escape(image_path.string()) +
                    "\"]}\n");
  bool const completed = output_buffer.wait_contains("rpc image answer", std::chrono::seconds(2));
  input_buffer.close();
  rpc_thread.join();
  auto const jsonl = output_buffer.str();
  auto entries = ava::app::runtime::session_ts::rat(unlocked_session)->store.load();
  expect(result.has_value(), "RPC image prompt loop completes successfully");
  expect(completed && jsonl.find("\"id\":\"p-img\"") != std::string::npos && jsonl.find("\"success\":true") != std::string::npos,
         "RPC image prompt returns a successful prompt response");
  expect(transport.requests().size() == 1 && transport.requests()[0].body.find("\"type\":\"input_image\"") != std::string::npos &&
             transport.requests()[0].body.find("data:image/png;base64,") != std::string::npos,
         "RPC image prompt imports local image paths into provider image payloads");
  auto const persisted_metadata = entries && std::ranges::any_of(*entries, [](ava::session::SessionEntry const& entry) {
                                    return entry.type == ava::session::EntryType::UserMessage && entry.data_json.find("\"attachments\"") != std::string::npos &&
                                           entry.data_json.find("\"storage_path\":\"attachments/") != std::string::npos &&
                                           entry.data_json.find("data_base64") == std::string::npos;
                                  });
  expect(persisted_metadata, "RPC image prompt persists session-owned attachment metadata without raw image bytes");
}

void test_app_rpc_prompt_imports_inline_image_uploads()
{
  auto const root = create_empty_root("app-rpc-prompt-inline-image-upload");

  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::runtime::OpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.mode = ava::agent::Mode::Build;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC inline image upload prompt test opens runtime session");
  if (!session)
    return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({ava::provider::HttpResponse{
      .status_code = 200,
      .headers = {},
      .body = "data: {\"type\":\"response.output_text.delta\",\"delta\":\"rpc upload answer\"}\n\n"
              "data: [DONE]\n\n",
  }});
  ava::app::runtime::RunOptions runtime_options;
  runtime_options.access_token = "token";
  BlockingInputBuf input_buffer;
  std::istream in(&input_buffer);
  ThreadSafeStringBuf output_buffer;
  std::ostream out(&output_buffer);
  ava::core::VoidResult result;
  ava::app::runtime::session_ts unlocked_session(std::move(*session));
  std::jthread rpc_thread([&] {
    result = ava::app::run_rpc_loop(unlocked_session, open_options, provider, transport, runtime_options, in, out, [&] noexcept { input_buffer.close(); });
  });
  input_buffer.push("{\"id\":\"p-upload\",\"type\":\"prompt\",\"message\":\"describe\",\"images\":[{\"type\":\"image\",\"data\":\"" +
                    ava::provider::base64_encode(rpc_tiny_png_bytes()) + "\",\"mimeType\":\"image/png\"}]}\n");
  bool const completed = output_buffer.wait_contains("rpc upload answer", std::chrono::seconds(2));
  input_buffer.close();
  rpc_thread.join();
  auto const jsonl = output_buffer.str();
  auto entries = ava::app::runtime::session_ts::rat(unlocked_session)->store.load();
  expect(result.has_value(), "RPC inline image upload prompt loop completes successfully");
  expect(completed && jsonl.find("\"id\":\"p-upload\"") != std::string::npos && jsonl.find("\"success\":true") != std::string::npos,
         "RPC inline image upload prompt returns a successful prompt response");
  expect(transport.requests().size() == 1 && transport.requests()[0].body.find("\"type\":\"input_image\"") != std::string::npos &&
             transport.requests()[0].body.find("data:image/png;base64,") != std::string::npos,
         "RPC inline image uploads are imported into provider image payloads");
  auto const persisted_metadata = entries && std::ranges::any_of(*entries, [](ava::session::SessionEntry const& entry) {
                                    return entry.type == ava::session::EntryType::UserMessage && entry.data_json.find("\"attachments\"") != std::string::npos &&
                                           entry.data_json.find("\"mime_type\":\"image/png\"") != std::string::npos &&
                                           entry.data_json.find("\"storage_path\":\"attachments/") != std::string::npos &&
                                           entry.data_json.find("data_base64") == std::string::npos;
                                  });
  expect(persisted_metadata, "RPC inline image upload persists metadata without raw image bytes");
}

void test_app_rpc_prompt_rejects_inline_image_upload_mime_mismatch()
{
  auto const root = create_empty_root("app-rpc-prompt-inline-image-upload-mismatch");

  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::runtime::OpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.mode = ava::agent::Mode::Build;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC inline image MIME mismatch test opens runtime session");
  if (!session)
    return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({});
  ava::app::runtime::RunOptions runtime_options;
  runtime_options.access_token = "token";
  std::istringstream in("{\"id\":\"p-upload-bad\",\"type\":\"prompt\",\"message\":\"describe\",\"images\":[{\"type\":\"image\",\"data\":\"" +
                        ava::provider::base64_encode(rpc_tiny_png_bytes()) + "\",\"mimeType\":\"image/jpeg\"}]}\n");
  std::ostringstream out;

  ava::app::runtime::session_ts unlocked_session(std::move(*session));
  auto result = ava::app::run_rpc_loop(unlocked_session, open_options, provider, transport, runtime_options, in, out, ava::app::rpc::RpcInputWake{});
  auto const jsonl = out.str();
  expect(result.has_value(), "RPC inline image MIME mismatch loop completes after error response");
  expect(jsonl.find("\"id\":\"p-upload-bad\"") != std::string::npos && jsonl.find("\"success\":false") != std::string::npos &&
             jsonl.find("image attachment MIME type does not match detected bytes") != std::string::npos,
         "RPC inline image upload MIME mismatches are machine-readable error responses");
  expect(transport.requests().empty(), "RPC inline image MIME mismatch avoids dispatching a provider request");
}

void test_app_rpc_prompt_streams_provider_deltas_before_final_response()
{
  auto const root = create_empty_root("app-rpc-prompt-streaming");

  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::runtime::OpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.mode = ava::agent::Mode::Build;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC streaming prompt test opens runtime session");
  if (!session)
    return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ChunkedStreamingTransport transport({"data: {\"type\":\"response.output_text.delta\",\"delta\":\"rpc \"}\n\n",
                                       "data: {\"type\":\"response.output_text.delta\",\"delta\":\"stream\"}\n\n", "data: [DONE]\n\n"});
  ava::app::runtime::RunOptions runtime_options;
  runtime_options.access_token = "token";
  BlockingInputBuf input_buffer;
  std::istream in(&input_buffer);
  ThreadSafeStringBuf output_buffer;
  std::ostream out(&output_buffer);
  ava::core::VoidResult result;
  ava::app::runtime::session_ts unlocked_session(std::move(*session));
  std::jthread rpc_thread([&] {
    result = ava::app::run_rpc_loop(unlocked_session, open_options, provider, transport, runtime_options, in, out, [&] noexcept { input_buffer.close(); });
  });
  input_buffer.push("{\"id\":\"p1\",\"type\":\"prompt\",\"message\":\"hello rpc stream\"}\n");
  bool const completed = output_buffer.wait_contains("\"success\":true", std::chrono::seconds(2));
  input_buffer.close();
  rpc_thread.join();
  auto const jsonl = output_buffer.str();
  auto const update_position = jsonl.find("\"name\":\"message_update\"");
  auto const final_position = jsonl.find("\"name\":\"assistant_message\"");
  auto const response_position = jsonl.find("\"type\":\"response\"");
  expect(result.has_value(), "RPC streaming prompt loop completes successfully");
  expect(update_position != std::string::npos && final_position != std::string::npos && completed && response_position != std::string::npos &&
             update_position < final_position && final_position < response_position && jsonl.find("rpc stream") != std::string::npos,
         "RPC prompt emits live provider deltas before final assistant event and command response");
}

void test_app_rpc_prompt_retry_transport_cancellation_is_canceled_event()
{
  auto const root = create_empty_root("app-rpc-prompt-retry-canceled");

  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::runtime::OpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.mode = ava::agent::Mode::Build;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC retry-cancel prompt test opens runtime session");
  if (!session)
    return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({ava::provider::HttpResponse{.status_code = 500, .headers = {}, .body = "{\"error\":{\"message\":\"retry later\"}}"},
                                       ava::provider::HttpResponse{.status_code = 500, .headers = {}, .body = "{\"error\":{\"message\":\"retry later\"}}"},
                                       ava::provider::HttpResponse{.status_code = 500, .headers = {}, .body = "{\"error\":{\"message\":\"retry later\"}}"}});
  ava::app::runtime::RunOptions runtime_options;
  runtime_options.access_token = "token";
  runtime_options.enable_transport_retries = true;

  BlockingInputBuf input_buffer;
  std::istream in(&input_buffer);
  ThreadSafeStringBuf output_buffer;
  std::ostream out(&output_buffer);
  ava::core::VoidResult result;
  ava::app::runtime::session_ts unlocked_session(std::move(*session));
  std::jthread rpc_thread([&] {
    result = ava::app::run_rpc_loop(unlocked_session, open_options, provider, transport, runtime_options, in, out, [&] noexcept { input_buffer.close(); });
  });
  input_buffer.push("{\"id\":\"p-cancel\",\"type\":\"prompt\",\"message\":\"cancel during retry\"}\n");
  bool const retry_started = output_buffer.wait_contains("\"name\":\"retry\"", std::chrono::seconds(2));
  input_buffer.push("{\"id\":\"cancel\",\"type\":\"cancel\"}\n");
  bool const completed = output_buffer.wait_contains("\"id\":\"p-cancel\"", std::chrono::seconds(2));
  input_buffer.close();
  rpc_thread.join();
  auto const jsonl = output_buffer.str();
  expect(result.has_value(), "RPC retry-cancel prompt loop returns control after canceled run");
  expect(retry_started, "RPC retry-cancel prompt reaches retry backoff before cancellation");
  expect(completed, "RPC retry-cancel prompt produces a response before input closes");
  expect(!transport.requests().empty() && transport.requests().size() <= 2,
         "RPC retry-cancel prompt dispatches a bounded number of provider requests before cancellation");
  expect(jsonl.find("\"name\":\"canceled\"") != std::string::npos && jsonl.find("\"payload_type\":\"cancellation\"") != std::string::npos &&
             jsonl.find("\"name\":\"error\"") == std::string::npos,
         "RPC retry transport cancellation emits terminal canceled envelope instead of an error envelope");
}

void test_app_rpc_prompt_after_idle_cancel_clears_cancel_flag()
{
  auto const root = create_empty_root("app-rpc-prompt-after-idle-cancel");

  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::runtime::OpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.mode = ava::agent::Mode::Build;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC idle-cancel prompt test opens runtime session");
  if (!session)
    return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({ava::provider::HttpResponse{
      .status_code = 200,
      .headers = {},
      .body = "data: {\"type\":\"response.output_text.delta\",\"delta\":\"after cancel\"}\n\n"
              "data: [DONE]\n\n",
  }});
  ava::app::runtime::RunOptions runtime_options;
  runtime_options.access_token = "token";
  BlockingInputBuf input_buffer;
  std::istream in(&input_buffer);
  ThreadSafeStringBuf output_buffer;
  std::ostream out(&output_buffer);
  ava::core::VoidResult result;
  ava::app::runtime::session_ts unlocked_session(std::move(*session));
  std::jthread rpc_thread([&] {
    result = ava::app::run_rpc_loop(unlocked_session, open_options, provider, transport, runtime_options, in, out, [&] noexcept { input_buffer.close(); });
  });
  input_buffer.push("{\"id\":\"cancel-idle\",\"type\":\"cancel\"}\n");
  bool const canceled = output_buffer.wait_contains("\"id\":\"cancel-idle\"", std::chrono::seconds(2));
  input_buffer.push("{\"id\":\"p-after\",\"type\":\"prompt\",\"message\":\"run after idle cancel\"}\n");
  bool const completed = output_buffer.wait_contains("after cancel", std::chrono::seconds(2));
  input_buffer.close();
  rpc_thread.join();
  auto const jsonl = output_buffer.str();
  expect(result.has_value(), "RPC idle-cancel prompt loop completes successfully");
  expect(canceled, "RPC idle cancel writes a response");
  expect(completed && transport.requests().size() == 1 && jsonl.find("\"id\":\"p-after\"") != std::string::npos &&
             jsonl.find("\"success\":true") != std::string::npos && jsonl.find("after cancel") != std::string::npos,
         "RPC prompt after idle cancel clears the latched cancel flag and runs normally");
}

void test_app_rpc_prompt_refreshes_expired_oauth_before_provider_request()
{
  auto const root = create_empty_root("app-rpc-oauth-refresh");

  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  auto stored = ava::config::store_openai_credential(paths, ava::config::OpenAICredential{.type = ava::config::OpenAICredentialType::OAuth,
                                                                                          .access_token = "expired-rpc-access",
                                                                                          .refresh_token = "rpc-refresh",
                                                                                          .expires_at = 100,
                                                                                          .account_id = "acct_old",
                                                                                          .source_path = {}});
  expect(stored.has_value(), "RPC OAuth refresh test stores expired credential");

  ava::app::runtime::OpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.mode = ava::agent::Mode::Build;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC OAuth refresh test opens runtime session");
  if (!session)
    return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({ava::provider::HttpResponse{
                                           .status_code = 200,
                                           .headers = {},
                                           .body = "{\"access_token\":\"rpc-refreshed-access\","
                                                   "\"refresh_token\":\"rpc-rotated-refresh\","
                                                   "\"expires_in\":3600,\"account_id\":\"acct_rpc\"}",
                                       },
                                       ava::provider::HttpResponse{
                                           .status_code = 200,
                                           .headers = {},
                                           .body = "data: {\"type\":\"response.output_text.delta\",\"delta\":"
                                                   "\"rpc refreshed answer\"}\n\n"
                                                   "data: [DONE]\n\n",
                                       }});
  BlockingInputBuf input_buffer;
  std::istream in(&input_buffer);
  ThreadSafeStringBuf output_buffer;
  std::ostream out(&output_buffer);
  ava::core::VoidResult result;
  ava::app::runtime::session_ts unlocked_session(std::move(*session));
  std::jthread rpc_thread([&] {
    result = ava::app::run_rpc_loop(unlocked_session, open_options, provider, transport, ava::app::runtime::RunOptions{}, in, out,
                                    [&] noexcept { input_buffer.close(); });
  });
  input_buffer.push("{\"id\":\"p1\",\"type\":\"prompt\",\"message\":\"hello refreshed rpc\"}\n");
  bool const completed = output_buffer.wait_contains("\"success\":true", std::chrono::seconds(2));
  input_buffer.close();
  rpc_thread.join();
  auto const jsonl = output_buffer.str();
  expect(result.has_value(), "RPC prompt with expired OAuth completes after refresh");
  expect(transport.requests().size() == 2 && transport.requests()[0].url == "https://auth.openai.com/oauth/token" &&
             transport.requests()[1].headers.at("Authorization") == "Bearer rpc-refreshed-access" &&
             transport.requests()[1].headers.at("ChatGPT-Account-Id") == "acct_rpc",
         "RPC prompt refreshes OAuth before sending provider request");
  expect(completed && jsonl.find("rpc refreshed answer") != std::string::npos && jsonl.find("\"success\":true") != std::string::npos,
         "RPC prompt returns refreshed OAuth provider response");
  auto persisted = ava::config::load_openai_credential(paths);
  expect(persisted && persisted->has_value() && (*persisted)->access_token == "rpc-refreshed-access" && (*persisted)->refresh_token == "rpc-rotated-refresh",
         "RPC OAuth preflight persists refreshed credential before provider startup");
}

void test_app_rpc_malformed_line_recovery_and_unknown_command()
{
  auto const root = create_empty_root("app-rpc-recovery");

  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::runtime::OpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC recovery test opens runtime session");
  if (!session)
    return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({});
  std::istringstream in(
      "not json\n{\"id\":\"s1\",\"type\":\"get_state\"}\n"
      "{\"id\":\"u1\",\"type\":\"unknown\"}\n");
  std::ostringstream out;
  ava::app::runtime::session_ts unlocked_session(std::move(*session));
  auto result =
      ava::app::run_rpc_loop(unlocked_session, open_options, provider, transport, ava::app::runtime::RunOptions{}, in, out, ava::app::rpc::RpcInputWake{});
  auto const jsonl = out.str();
  expect(result.has_value(), "RPC loop continues after malformed and unknown commands");
  expect(std::count(jsonl.begin(), jsonl.end(), '\n') == 3 && jsonl.find("\"id\":\"\"") != std::string::npos &&
             jsonl.find("malformed RPC JSON object") != std::string::npos && jsonl.find("\"id\":\"s1\"") != std::string::npos &&
             jsonl.find("\"session_id\":\"") != std::string::npos && jsonl.find("\"id\":\"u1\"") != std::string::npos &&
             jsonl.find("unknown RPC command type") != std::string::npos,
         "RPC loop writes error responses and recovers for subsequent JSONL records");
}

void test_app_rpc_state_list_sessions_and_open_session()
{
  auto const root = create_empty_root("app-rpc-state");

  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::runtime::OpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto first = ava::app::open_runtime_session(open_options);
  auto second = ava::app::open_runtime_session(open_options);
  expect(first.has_value() && second.has_value(), "RPC state test opens multiple sessions");
  if (!first || !second)
    return;
  auto const first_id = first->store.session_id();
  auto const second_id = second->store.session_id();
  first = std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "release target runtime before RPC switch"));

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({});
  std::istringstream in(
      "{\"id\":\"state\",\"type\":\"get_state\"}\n"
      "{\"id\":\"list\",\"type\":\"list_sessions\"}\n"
      "{\"id\":\"open\",\"type\":\"open_session\",\"session_id\":\"" +
      first_id + "\"}\n");
  std::ostringstream out;
  ava::app::runtime::session_ts unlocked_second(std::move(*second));
  auto result =
      ava::app::run_rpc_loop(unlocked_second, open_options, provider, transport, ava::app::runtime::RunOptions{}, in, out, ava::app::rpc::RpcInputWake{});
  auto const jsonl = out.str();
  expect(result.has_value(), "RPC state/list/open loop completes successfully");
  expect(jsonl.find("\"id\":\"state\"") != std::string::npos && jsonl.find(second_id) != std::string::npos &&
             jsonl.find("\"id\":\"list\"") != std::string::npos && jsonl.find(first_id) != std::string::npos &&
             jsonl.find("\"id\":\"open\"") != std::string::npos,
         "RPC state, list_sessions, and open_session return session metadata");
  expect(ava::app::runtime::session_ts::rat(unlocked_second)->store.session_id() == first_id, "RPC open_session switches the active runtime session");
}

void test_app_rpc_job_controls_are_active_safe_and_redacted()
{
  auto const root = temp_root() / "app-rpc-job-controls";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  ava::app::runtime::OpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC job fixture opens runtime session");
  if (!session || !session->subagent_coordinator())
    return;

  struct WorkerState
  {
    std::mutex mutex;
    std::condition_variable changed;
    bool started = false;
    bool release = false;
    ava::agent::BackgroundJobCompletion run(ava::agent::BackgroundJobContext const& context)
    {
      std::stop_callback wake(context.stop_token, [&] { changed.notify_all(); });
      std::unique_lock lock(mutex);
      started = true;
      changed.notify_all();
      changed.wait(lock, [&] { return release || context.stop_token.stop_requested(); });
      if (context.stop_token.stop_requested())
        return {.state = ava::agent::BackgroundJobState::Canceled,
                .final_text = {},
                .stop_reason = "canceled",
                .error = std::nullopt,
                .provider_iterations = 0,
                .tool_calls = 0,
                .tool_iterations = 0};
      return {.state = ava::agent::BackgroundJobState::Completed,
              .final_text = "bounded RPC result",
              .stop_reason = "completed",
              .error = std::nullopt,
              .provider_iterations = 0,
              .tool_calls = 0,
              .tool_iterations = 0};
    }
    bool wait_started()
    {
      std::unique_lock lock(mutex);
      return changed.wait_for(lock, std::chrono::seconds(1), [&] { return started; });
    }
    void finish()
    {
      std::lock_guard lock(mutex);
      release = true;
      changed.notify_all();
    }
  };

  auto coordinator = session->subagent_coordinator();
  auto const owner = session->store.session_id();
  auto promoted_state = std::make_shared<WorkerState>();
  auto canceled_state = std::make_shared<WorkerState>();
  auto promotable = coordinator->start(owner, ava::agent::SubagentJobMode::Foreground, {.child_session_id = "rpc_promotable"},
                                       [promoted_state](auto const& context) { return promoted_state->run(context); });
  auto cancelable = coordinator->start(owner, ava::agent::SubagentJobMode::Foreground, {.child_session_id = "rpc_cancelable"},
                                       [canceled_state](auto const& context) { return canceled_state->run(context); });
  auto failed = coordinator->start(owner, ava::agent::SubagentJobMode::Foreground, {.child_session_id = "rpc_failed"}, [](auto const&) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Provider, "credential=rpc-secret raw provider body");
    error.with_context("command", "curl --token rpc-secret");
    return ava::agent::BackgroundJobCompletion{.state = ava::agent::BackgroundJobState::Failed,
                                               .final_text = {},
                                               .stop_reason = "failed",
                                               .error = std::move(error),
                                               .provider_iterations = 0,
                                               .tool_calls = 0,
                                               .tool_iterations = 0};
  });
  expect(promotable && cancelable && failed && promoted_state->wait_started() && canceled_state->wait_started(), "RPC job fixture starts controlled jobs");
  if (!promotable || !cancelable || !failed)
    return;
  static_cast<void>(coordinator->wait(owner, failed->job.identity.job_id, std::chrono::seconds(1)));

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  BlockingResponseTransport transport(sse_response(final_text_sse("parent terminal")));
  ava::app::runtime::RunOptions runtime_options;
  runtime_options.access_token = "token";
  BlockingInputBuf input_buffer;
  std::istream in(&input_buffer);
  ThreadSafeStringBuf output_buffer;
  std::ostream out(&output_buffer);
  ava::core::VoidResult rpc_result;
  ava::app::runtime::session_ts unlocked_session(std::move(*session));
  std::jthread rpc_thread([&] {
    rpc_result = ava::app::run_rpc_loop(unlocked_session, open_options, provider, transport, runtime_options, in, out, [&] noexcept { input_buffer.close(); });
  });
  input_buffer.push("{\"id\":\"prompt\",\"type\":\"prompt\",\"message\":\"keep active\"}\n");
  expect(transport.wait_for_request(std::chrono::seconds(2)), "RPC job fixture has an active parent prompt");
  auto const promotable_id = promotable->job.identity.job_id;
  auto const cancelable_id = cancelable->job.identity.job_id;
  auto const failed_id = failed->job.identity.job_id;
  input_buffer.push("{\"id\":\"jobs\",\"type\":\"list_jobs\"}\n");
  input_buffer.push("{\"id\":\"status\",\"type\":\"get_job\",\"job_id\":\"" + promotable_id + "\"}\n");
  input_buffer.push("{\"id\":\"wait\",\"type\":\"wait_job\",\"job_id\":\"" + promotable_id + "\",\"timeout_ms\":1}\n");
  input_buffer.push("{\"id\":\"not-ready\",\"type\":\"get_job_result\",\"job_id\":\"" + promotable_id + "\"}\n");
  input_buffer.push("{\"id\":\"promote\",\"type\":\"promote_job\",\"job_id\":\"" + promotable_id + "\"}\n");
  input_buffer.push("{\"id\":\"cancel-job\",\"type\":\"cancel_job\",\"job_id\":\"" + cancelable_id + "\"}\n");
  input_buffer.push("{\"id\":\"failed-result\",\"type\":\"get_job_result\",\"job_id\":\"" + failed_id + "\"}\n");
  bool const controls_completed = output_buffer.wait_contains("\"id\":\"failed-result\"", std::chrono::seconds(2));
  auto active_output = output_buffer.str();
  expect(controls_completed && active_output.find("\"id\":\"promote\"") != std::string::npos &&
             active_output.find("\"was_promoted\":true") != std::string::npos && active_output.find("\"id\":\"cancel-job\"") != std::string::npos &&
             active_output.find("\"timed_out\":true") != std::string::npos && active_output.find("\"code\":\"job_not_ready\"") != std::string::npos &&
             active_output.find("\"message\":\"subagent job failed\"") != std::string::npos && active_output.find("rpc-secret") == std::string::npos &&
             active_output.find("raw provider body") == std::string::npos && active_output.find("\"code\":\"active_run\"") == std::string::npos,
         "RPC job status, wait, result, cancel, and promote remain active-safe and redact provider/error context");

  promoted_state->finish();
  static_cast<void>(coordinator->wait(owner, promotable_id, std::chrono::seconds(1)));
  transport.release();
  input_buffer.close();
  rpc_thread.join();
  expect(rpc_result.has_value(), "RPC job active-safe loop shuts down cleanly");
}

void test_app_rpc_current_session_reads_reject_path_replacement()
{
  auto const root = create_empty_root("app-rpc-current-session-replacement");

  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::runtime::OpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "replacement-safe current-session RPC test opens runtime session");
  if (!session)
    return;
  expect(session
             ->append_owned(ava::session::SessionEntry{.id = "original_rpc_history",
                                                       .parent_id = "",
                                                       .type = ava::session::EntryType::UserMessage,
                                                       .timestamp = "2026-05-02T00:00:00Z",
                                                       .data_json = "{\"text\":\"ORIGINAL_RPC_HISTORY\"}"})
             .has_value(),
         "replacement-safe current-session RPC test seeds original history");

  auto replacement = ava::session::serialize_session_entry_line(ava::session::SessionEntry{.id = "replacement_rpc_history",
                                                                                           .parent_id = "",
                                                                                           .type = ava::session::EntryType::UserMessage,
                                                                                           .timestamp = "2026-05-02T00:00:01Z",
                                                                                           .data_json = "{\"text\":\"RPC_REPLACEMENT_CANARY\"}"});
  expect(replacement.has_value(), "replacement-safe current-session RPC test serializes replacement history");
  if (!replacement)
    return;
  bool replaced = false;
  auto const session_path = session->store.session_path();
  session->store.set_after_lease_bound_read_for_test([&, session_path] {
    if (replaced)
      return;
    replaced = true;
    std::filesystem::rename(session_path, session_path.string() + ".parked");
    std::ofstream file(session_path, std::ios::binary | std::ios::trunc);
    file << *replacement << '\n';
  });

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({});
  std::istringstream in(
      "{\"id\":\"messages\",\"type\":\"get_messages\"}\n"
      "{\"id\":\"metadata\",\"type\":\"session_metadata\"}\n"
      "{\"id\":\"stats\",\"type\":\"get_session_stats\"}\n"
      "{\"id\":\"validate\",\"type\":\"validate_session\"}\n");
  std::ostringstream out;
  ava::app::runtime::session_ts unlocked_session(std::move(*session));
  auto result =
      ava::app::run_rpc_loop(unlocked_session, open_options, provider, transport, ava::app::runtime::RunOptions{}, in, out, ava::app::rpc::RpcInputWake{});
  auto const jsonl = out.str();
  auto pathname_entries = ava::app::runtime::session_ts::rat(unlocked_session)->store.load();
  expect(result && replaced && jsonl.find("replaced") != std::string::npos && jsonl.find("RPC_REPLACEMENT_CANARY") == std::string::npos && pathname_entries &&
             pathname_entries->size() == 1 && pathname_entries->front().data_json.find("RPC_REPLACEMENT_CANARY") != std::string::npos,
         "current-session RPC messages, metadata, stats, and validation fail closed after authority binding without serializing replacement content");
}

void test_app_rpc_session_metadata_name_and_labels()
{
  auto const root = create_empty_root("app-rpc-session-metadata");

  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::runtime::OpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC session metadata test opens runtime session");
  if (!session)
    return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({});
  std::istringstream in(
      "{\"id\":\"initial\",\"type\":\"session_metadata\"}\n"
      "{\"id\":\"name\",\"type\":\"set_session_name\",\"session_name\":\"Auth follow-up\"}\n"
      "{\"id\":\"labels\",\"type\":\"set_session_labels\",\"labels\":[\"auth\",\"bug\"]}\n"
      "{\"id\":\"after\",\"type\":\"session_metadata\"}\n"
      "{\"id\":\"bad\",\"type\":\"set_session_labels\",\"labels\":[\"dup\",\"dup\"]}\n");
  std::ostringstream out;
  ava::app::runtime::session_ts unlocked_session(std::move(*session));
  auto result =
      ava::app::run_rpc_loop(unlocked_session, open_options, provider, transport, ava::app::runtime::RunOptions{}, in, out, ava::app::rpc::RpcInputWake{});
  auto const jsonl = out.str();
  auto metadata = ava::session::load_session_metadata(ava::app::runtime::session_ts::rat(unlocked_session)->store);
  auto entries = ava::app::runtime::session_ts::rat(unlocked_session)->store.load();
  auto validation = entries ? ava::session::validate_session_replay(*entries) : ava::session::SessionReplayValidation{};

  expect(result.has_value() && metadata && metadata->name == "Auth follow-up" && metadata->labels.size() == 2 && metadata->labels[0] == "auth" &&
             metadata->labels[1] == "bug" && metadata->actor == "rpc" && validation.ok(),
         "RPC session metadata commands persist append-only name and label entries");
  expect(jsonl.find("\"id\":\"initial\"") != std::string::npos && jsonl.find("\"name\":\"\"") != std::string::npos &&
             jsonl.find("\"id\":\"name\"") != std::string::npos && jsonl.find("\"name\":\"Auth follow-up\"") != std::string::npos &&
             jsonl.find("\"labels\":[\"auth\",\"bug\"]") != std::string::npos && jsonl.find("\"actor\":\"rpc\"") != std::string::npos &&
             jsonl.find("session labels must be unique") != std::string::npos,
         "RPC session metadata responses expose current name/labels/actor and reject invalid labels");
}

void test_app_rpc_session_tree_command_and_switch_navigation()
{
  auto const root = create_empty_root("app-rpc-session-tree");

  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::runtime::OpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto parent = ava::app::open_runtime_session(open_options);
  auto child = ava::app::open_runtime_session(open_options);
  expect(parent.has_value() && child.has_value(), "RPC session_tree test opens parent and child sessions");
  if (!parent || !child)
    return;
  auto const parent_id = parent->store.session_id();
  auto const child_id = child->store.session_id();
  auto parent_entries = parent->store.load();
  expect(parent_entries && !parent_entries->empty(), "RPC session_tree test loads parent start entry");
  if (!parent_entries || parent_entries->empty())
    return;

  ava::session::SessionMetadataUpdate parent_metadata;
  parent_metadata.name = "Parent";
  parent_metadata.labels = std::vector<std::string>{"root"};
  parent_metadata.branch_origin = "root";
  parent_metadata.actor = "test";
  auto parent_meta = ava::app::append_runtime_session_metadata(*parent, std::move(parent_metadata));

  ava::session::SessionMetadataUpdate child_metadata;
  child_metadata.name = "Child";
  child_metadata.labels = std::vector<std::string>{"branch"};
  child_metadata.archived = true;
  child_metadata.parent_session_id = parent_id;
  child_metadata.source_session_id = parent_id;
  child_metadata.branch_from_entry_id = parent_entries->front().id;
  child_metadata.branch_origin = "fork";
  child_metadata.actor = "test";
  auto child_meta = ava::app::append_runtime_session_metadata(*child, std::move(child_metadata));
  expect(parent_meta && child_meta, "RPC session_tree test persists branch metadata");
  parent = std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "release parent runtime before RPC switch"));

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({});
  auto const requests = std::string("{\"id\":\"tree\",\"type\":\"session_tree\"}\n") + "{\"id\":\"switch\",\"type\":\"switch_session\",\"session_id\":\"" +
                        parent_id + "\"}\n" + "{\"id\":\"tree2\",\"type\":\"session_tree\"}\n";
  std::istringstream in(requests);
  std::ostringstream out;
  ava::app::runtime::session_ts unlocked_child(std::move(*child));
  auto result =
      ava::app::run_rpc_loop(unlocked_child, open_options, provider, transport, ava::app::runtime::RunOptions{}, in, out, ava::app::rpc::RpcInputWake{});
  auto const jsonl = out.str();
  expect(result.has_value(), "RPC session_tree loop completes successfully");
  expect(jsonl.find("\"id\":\"tree\"") != std::string::npos && jsonl.find("\"current_session_id\":\"" + child_id + "\"") != std::string::npos &&
             jsonl.find("\"path\":[\"" + parent_id + "\",\"" + child_id + "\"]") != std::string::npos &&
             jsonl.find("\"parent_session_id\":\"" + parent_id + "\"") != std::string::npos &&
             jsonl.find("\"children\":[\"" + child_id + "\"]") != std::string::npos && jsonl.find("\"labels\":[\"branch\"]") != std::string::npos &&
             jsonl.find("\"labels_updated\":\"") != std::string::npos && jsonl.find("\"archived\":true") != std::string::npos &&
             jsonl.find("\"actor\":\"test\"") != std::string::npos,
         "RPC session_tree returns current path, children, labels, archive state, actor, and provenance metadata");
  expect(jsonl.find("\"id\":\"switch\"") != std::string::npos && jsonl.find("\"current_session_id\":\"" + parent_id + "\"") != std::string::npos &&
             ava::app::runtime::session_ts::rat(unlocked_child)->store.session_id() == parent_id,
         "RPC switch_session navigates the active session used by following tree calls");
}

void test_app_rpc_session_fork_and_clone_commands()
{
  auto const root = create_empty_root("app-rpc-session-branch");

  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::runtime::OpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC session branch test opens runtime session");
  if (!session)
    return;
  auto const source_id = session->store.session_id();
  auto source_entries = session->store.load();
  expect(source_entries && !source_entries->empty(), "RPC session branch test loads source start entry");
  if (!source_entries || source_entries->empty())
    return;
  auto const branch_from_entry_id = source_entries->front().id;
  auto const source_count = source_entries->size();
  auto const valid_source_bytes = read_rpc_golden(session->store.session_path());
  {
    std::ofstream file(session->store.session_path(), std::ios::binary | std::ios::app);
    file << "{\"version\":3,\"id\":\"rpc-current-torn";
  }

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({});
  auto const requests = std::string(
                            "{\"id\":\"fork\",\"type\":\"fork_session\","
                            "\"branch_from_entry_id\":\"") +
                        branch_from_entry_id +
                        "\",\"session_name\":\"Forked\",\"labels\":[\"forked\"]}\n"
                        "{\"id\":\"fork_meta\",\"type\":\"session_metadata\"}\n"
                        "{\"id\":\"clone\",\"type\":\"clone_session\",\"session_id\":\"" +
                        source_id +
                        "\",\"session_name\":\"Cloned\"}\n"
                        "{\"id\":\"clone_meta\",\"type\":\"session_metadata\"}\n";
  std::istringstream in(requests);
  std::ostringstream out;
  ava::app::runtime::session_ts unlocked_session(std::move(*session));
  auto result =
      ava::app::run_rpc_loop(unlocked_session, open_options, provider, transport, ava::app::runtime::RunOptions{}, in, out, ava::app::rpc::RpcInputWake{});
  auto const jsonl = out.str();
  auto source_store = ava::session::SessionStore::open(workspace, source_id, paths.sessions_dir);
  bool source_unchanged = false;
  if (source_store)
  {
    auto source_after = source_store->load();
    source_unchanged = source_after && source_after->size() == source_count && read_rpc_golden(source_store->session_path()) == valid_source_bytes;
  }
  expect(result.has_value(), "RPC fork/clone loop completes successfully");
  expect(source_unchanged, "RPC fork uses its existing lease to recover the current source and later branching does not add source entries");
  expect(jsonl.find("\"id\":\"fork\"") != std::string::npos && jsonl.find("\"id\":\"fork_meta\"") != std::string::npos &&
             jsonl.find("\"created\":true") != std::string::npos && jsonl.find("\"name\":\"Forked\"") != std::string::npos &&
             jsonl.find("\"labels\":[\"forked\"]") != std::string::npos && jsonl.find("\"parent_session_id\":\"" + source_id + "\"") != std::string::npos &&
             jsonl.find("\"branch_from_entry_id\":\"" + branch_from_entry_id + "\"") != std::string::npos &&
             jsonl.find("\"branch_origin\":\"fork\"") != std::string::npos,
         "RPC fork_session creates and switches to a fork with provenance metadata");
  expect(jsonl.find("\"id\":\"clone\"") != std::string::npos && jsonl.find("\"id\":\"clone_meta\"") != std::string::npos &&
             jsonl.find("\"name\":\"Cloned\"") != std::string::npos && jsonl.find("\"branch_origin\":\"clone\"") != std::string::npos,
         "RPC clone_session creates and switches to a clone with provenance metadata");
  auto active_destination_contender = ava::session::SessionLease::acquire(ava::app::runtime::session_ts::rat(unlocked_session)->store.session_path());
  expect(!active_destination_contender && active_destination_contender.error().message().find("already owned") != std::string::npos,
         "RPC fork/clone transfers the active destination lease directly into the replacement runtime");
}

void test_app_rpc_branch_construction_failure_rolls_back_created_file()
{
  auto const root = create_empty_root("app-rpc-branch-rollback");

  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::runtime::OpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto source = ava::app::open_runtime_session(open_options);
  expect(source.has_value(), "RPC rollback test opens an active source session");
  if (!source)
    return;
  auto entries = source->store.load();
  expect(entries && !entries->empty(), "RPC rollback test loads the source start entry");
  if (!entries || entries->empty())
    return;
  auto appended =
      source->append_owned(ava::session::SessionEntry{.id = "entry_rpc_rollback_attachment",
                                                      .parent_id = entries->back().id,
                                                      .type = ava::session::EntryType::UserMessage,
                                                      .timestamp = "2026-07-16T00:00:00Z",
                                                      .data_json = "{\"text\":\"attachment\",\"attachments\":[{\"id\":\"rpc_rollback_img\","
                                                                   "\"type\":\"image\",\"mime_type\":\"image/png\",\"byte_size\":5,"
                                                                   "\"sha256\":\"2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824\","
                                                                   "\"storage_path\":\"attachments/rollback.txt\"}]}",
                                                      .version = 2});
  auto const source_attachment = ava::session::attachment_storage_root(source->store) / "attachments" / "rollback.txt";
  write_app_test_file(source_attachment, "hello");
  expect(appended.has_value(), "RPC rollback test appends a copyable source attachment reference");
  if (!appended)
    return;

  auto const source_id = source->store.session_id();
  auto const source_path = source->store.session_path();
  std::filesystem::create_directories(paths.models_file);
  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({});
  std::istringstream in("{\"id\":\"fork-rollback\",\"type\":\"fork_session\"}\n");
  std::ostringstream out;
  ava::app::runtime::session_ts unlocked_source(std::move(*source));
  auto result =
      ava::app::run_rpc_loop(unlocked_source, open_options, provider, transport, ava::app::runtime::RunOptions{}, in, out, ava::app::rpc::RpcInputWake{});
  auto const jsonl = out.str();
  auto const marker = std::string("created_session_id: ");
  auto const marker_offset = jsonl.find(marker);
  std::optional<std::string> created_id;
  if (marker_offset != std::string::npos)
  {
    auto const value_start = marker_offset + marker.size();
    auto const value_end = jsonl.find("\\n", value_start);
    created_id = jsonl.substr(value_start, value_end == std::string::npos ? std::string::npos : value_end - value_start);
  }
  bool destination_jsonl_removed = false;
  bool destination_attachment_retained = false;
  if (created_id)
  {
    auto destination =
        ava::session::SessionStore(ava::session::SessionStoreOptions{.root_dir = paths.sessions_dir, .workspace_dir = workspace, .session_id = *created_id});
    auto const destination_attachment = ava::session::attachment_storage_root(destination) / "attachments" / "rollback.txt";
    destination_jsonl_removed = !std::filesystem::exists(destination.session_path());
    destination_attachment_retained = read_rpc_golden(destination_attachment) == "hello";
  }
  auto source_contender = ava::session::SessionLease::acquire(source_path);
  expect(result && created_id && jsonl.find("\"id\":\"fork-rollback\"") != std::string::npos && jsonl.find("\"success\":false") != std::string::npos &&
             jsonl.find("rollback_attachment_disposition: preserved") != std::string::npos && destination_jsonl_removed && destination_attachment_retained &&
             ava::app::runtime::session_ts::rat(unlocked_source)->store.session_id() == source_id && !source_contender &&
             source_contender.error().message().find("already owned") != std::string::npos,
         "RPC branch runtime-construction failure preserves the primary error, removes only destination JSONL, retains copied attachments, and leaves the "
         "source active");
}

void test_app_rpc_noncurrent_branch_source_recovers_torn_tail()
{
  auto const root = create_empty_root("app-rpc-noncurrent-torn-branch");

  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::runtime::OpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto source = ava::app::open_runtime_session(open_options);
  expect(source.has_value(), "RPC noncurrent torn branch test opens source runtime");
  if (!source)
    return;
  auto const source_id = source->store.session_id();
  auto const source_path = source->store.session_path();
  auto const valid_source_bytes = read_rpc_golden(source_path);
  source = std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "release source runtime before RPC branch"));

  auto current = ava::app::open_runtime_session(open_options);
  expect(current.has_value(), "RPC noncurrent torn branch test opens a different current runtime");
  if (!current)
    return;
  {
    std::ofstream file(source_path, std::ios::binary | std::ios::app);
    file << "{\"version\":3,\"id\":\"rpc-torn";
  }

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({});
  std::istringstream in("{\"id\":\"clone\",\"type\":\"clone_session\",\"session_id\":\"" + source_id + "\"}\n");
  std::ostringstream out;
  ava::app::runtime::session_ts unlocked_current(std::move(*current));
  auto result =
      ava::app::run_rpc_loop(unlocked_current, open_options, provider, transport, ava::app::runtime::RunOptions{}, in, out, ava::app::rpc::RpcInputWake{});
  ava::app::runtime::session_ts::rat current_r(unlocked_current);
  auto cloned_entries = current_r->store.load();
  auto cloned_metadata = ava::session::load_session_metadata(current_r->store);
  expect(result && out.str().find("\"id\":\"clone\"") != std::string::npos && current_r->store.session_id() != source_id && cloned_entries &&
             cloned_entries->size() == 2 && cloned_metadata && cloned_metadata->source_session_id == source_id && cloned_metadata->branch_origin == "clone" &&
             read_rpc_golden(source_path) == valid_source_bytes,
         "RPC branching temporarily leases and recovers a different source before holding it through clone creation");
}

void test_app_rpc_summarize_branch_appends_to_source_session()
{
  auto const root = create_empty_root("app-rpc-branch-summary");

  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::runtime::OpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC summarize_branch test opens runtime session");
  if (!session)
    return;
  auto const source_id = session->store.session_id();
  auto source_entries = session->store.load();
  expect(source_entries && !source_entries->empty(), "RPC summarize_branch test loads source start entry");
  if (!source_entries || source_entries->empty())
    return;
  auto const source_count = source_entries->size();
  auto const root_entry_id = source_entries->front().id;
  auto const tip_entry_id = source_entries->back().id;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({});
  auto const requests = std::string("{\"id\":\"summary\",\"type\":\"summarize_branch\",") + "\"branch_root_entry_id\":\"" + root_entry_id +
                        "\","
                        "\"branch_tip_entry_id\":\"" +
                        tip_entry_id +
                        "\","
                        "\"summary\":\"Abandoned path was not needed.\","
                        "\"provider\":\"openai\",\"model\":\"gpt-test\",\"reason\":\"test\"}\n"
                        "{\"id\":\"stats\",\"type\":\"get_session_stats\"}\n";
  std::istringstream in(requests);
  std::ostringstream out;
  ava::core::VoidResult latched;
  ava::app::runtime::session_ts unlocked_session(std::move(*session));
  {
    auto result =
        ava::app::run_rpc_loop(unlocked_session, open_options, provider, transport, ava::app::runtime::RunOptions{}, in, out, ava::app::rpc::RpcInputWake{});
    ava::app::runtime::session_ts::rat session_r(unlocked_session);
    auto const jsonl = out.str();
    auto source_store = ava::session::SessionStore::open(workspace, source_id, paths.sessions_dir);
    bool source_has_summary = false;
    if (source_store)
    {
      auto source_after = source_store->load();
      source_has_summary = source_after && source_after->size() == source_count + 1 && source_after->back().type == ava::session::EntryType::BranchSummary &&
                           source_after->back().parent_id == tip_entry_id;
    }
    expect(result.has_value(), "RPC summarize_branch loop completes successfully");
    expect(source_has_summary && session_r->store.session_id() == source_id, "RPC summarize_branch appends to the source session without switching sessions");
    expect(jsonl.find("\"id\":\"summary\"") != std::string::npos && jsonl.find("\"source_session_id\":\"" + source_id + "\"") != std::string::npos &&
               jsonl.find("\"type\":\"branch_summary\"") != std::string::npos && jsonl.find("Abandoned path was not needed.") != std::string::npos &&
               jsonl.find("\"branch_summary\":1") != std::string::npos,
           "RPC summarize_branch returns the persisted branch summary entry and updated stats expose the count");

    auto invalid = ava::session::SessionEntry{
        .id = "summary-route-latch", .parent_id = "", .type = ava::session::EntryType::Error, .timestamp = ava::session::now_timestamp(), .data_json = ""};
    latched = session_r->run_controller()
                  ? session_r->run_controller()->append(std::move(invalid))
                  : ava::core::VoidResult(std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "missing summary route controller")));
  }
  std::istringstream blocked_in(std::string("{\"id\":\"blocked-summary\",\"type\":\"summarize_branch\",") + "\"branch_root_entry_id\":\"" + root_entry_id +
                                "\",\"branch_tip_entry_id\":\"" + tip_entry_id +
                                "\",\"summary\":\"must not bypass latch\",\"provider\":\"openai\",\"model\":\"gpt-test\",\"reason\":\"test\"}\n");
  std::ostringstream blocked_out;
  auto blocked_loop = ava::app::run_rpc_loop(unlocked_session, open_options, provider, transport, ava::app::runtime::RunOptions{}, blocked_in, blocked_out,
                                             ava::app::rpc::RpcInputWake{});
  ava::app::runtime::session_ts::rat session_r(unlocked_session);
  auto blocked_entries = session_r->store.load();
  expect(!latched && blocked_loop && blocked_entries && blocked_entries->size() == source_count + 1 &&
             blocked_out.str().find("blocked-summary") != std::string::npos && blocked_out.str().find("append_commit_state") != std::string::npos,
         "current-session RPC summaries perform lease-bound reads but append only through the owner route and cannot bypass its persistence latch");
}

void test_app_rpc_model_commands()
{
  auto const root = create_empty_root("app-rpc-model-commands");

  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::runtime::OpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC model command test opens runtime session");
  if (!session)
    return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({});
  std::istringstream in(
      "{\"id\":\"list\",\"type\":\"list_models\"}\n"
      "{\"id\":\"set\",\"type\":\"set_model\",\"provider\":\"anthropic\","
      "\"model\":\"claude-sonnet-4-5\"}\n"
      "{\"id\":\"state\",\"type\":\"get_state\"}\n"
      "{\"id\":\"stats\",\"type\":\"get_session_stats\"}\n"
      "{\"id\":\"cycle\",\"type\":\"cycle_model\"}\n");
  std::ostringstream out;
  ava::app::runtime::session_ts unlocked_session(std::move(*session));
  auto result =
      ava::app::run_rpc_loop(unlocked_session, open_options, provider, transport, ava::app::runtime::RunOptions{}, in, out, ava::app::rpc::RpcInputWake{});
  auto const jsonl = out.str();
  expect(result.has_value(), "RPC model command loop completes successfully");
  expect(jsonl.find("\"id\":\"list\"") != std::string::npos && jsonl.find("\"models\"") != std::string::npos &&
             jsonl.find("claude-sonnet-4-5") != std::string::npos,
         "RPC list_models returns configured model catalog");
  expect(jsonl.find("\"id\":\"set\"") != std::string::npos && jsonl.find("\"provider\":\"anthropic\"") != std::string::npos &&
             jsonl.find("\"model\":\"claude-sonnet-4-5\"") != std::string::npos,
         "RPC set_model returns updated Anthropic state");
  expect(jsonl.find("\"id\":\"state\"") != std::string::npos && jsonl.find("\"model\":\"claude-sonnet-4-5\"") != std::string::npos,
         "RPC get_state reflects selected model after set_model");
  expect(jsonl.find("\"id\":\"stats\"") != std::string::npos && jsonl.find("\"model_change\":1") != std::string::npos,
         "RPC get_session_stats reports model_change count");
  expect(jsonl.find("\"id\":\"cycle\"") != std::string::npos && jsonl.find("\"provider\":\"deepseek\"") != std::string::npos,
         "RPC cycle_model advances to the next configured provider model");
  ava::app::runtime::session_ts::wat session_w(unlocked_session);
  expect(session_w->model().provider_id == "deepseek", "RPC cycle_model updates active session model");
  auto previous = ava::app::rpc::previous_runtime_model(*session_w);
  expect(previous && previous->provider_id == "anthropic" && previous->model_id == "claude-sonnet-4-5",
         "runtime previous model helper returns the configured predecessor for TUI reverse cycling");
  session_w->model_selection().scoped_model_cycle = std::vector<std::string>{"anthropic/claude-sonnet-4-5", "openai/gpt-5.5"};
  auto scoped_next = ava::app::rpc::next_runtime_model(*session_w);
  expect(scoped_next && scoped_next->provider_id == "anthropic" && scoped_next->model_id == "claude-sonnet-4-5",
         "runtime next model helper starts at the first scoped model when current model is outside the scoped cycle");
  if (scoped_next)
    session_w->model_selection().model = *scoped_next;
  auto scoped_previous = ava::app::rpc::previous_runtime_model(*session_w);
  expect(scoped_previous && scoped_previous->provider_id == "openai" && scoped_previous->model_id == "gpt-5.5",
         "runtime previous model helper wraps within the session-scoped model cycle");
  session_w->model_selection().scoped_model_cycle = std::vector<std::string>{};
  auto empty_scoped_next = ava::app::rpc::next_runtime_model(*session_w);
  expect(!empty_scoped_next && empty_scoped_next.error().message().find("enabled for cycling") != std::string::npos,
         "runtime model cycling fails visibly when the session-scoped model cycle is empty");
}

void test_app_rpc_reasoning_commands()
{
  auto const root = create_empty_root("app-rpc-reasoning-commands");

  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::runtime::OpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC reasoning command test opens runtime session");
  if (!session)
    return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({});
  std::istringstream in(
      "{\"id\":\"set\",\"type\":\"set_reasoning\",\"reasoning_level\":\"medium\"}\n"
      "{\"id\":\"state\",\"type\":\"get_state\"}\n"
      "{\"id\":\"invalid\",\"type\":\"set_reasoning\",\"reasoning_level\":\"ultra\"}\n"
      "{\"id\":\"set-off\",\"type\":\"set_reasoning\",\"reasoning_level\":\" off \"}\n"
      "{\"id\":\"state-off\",\"type\":\"get_state\"}\n"
      "{\"id\":\"clear\",\"type\":\"clear_reasoning\"}\n");
  std::ostringstream out;
  ava::app::runtime::session_ts unlocked_session(std::move(*session));
  auto result =
      ava::app::run_rpc_loop(unlocked_session, open_options, provider, transport, ava::app::runtime::RunOptions{}, in, out, ava::app::rpc::RpcInputWake{});
  ava::app::runtime::session_ts::rat session_r(unlocked_session);
  auto const jsonl = out.str();
  expect(result.has_value(), "RPC reasoning command loop completes successfully");
  expect(jsonl.find("\"id\":\"set\"") != std::string::npos && jsonl.find("\"reasoning_enabled\":true") != std::string::npos &&
             jsonl.find("\"reasoning_level\":\"medium\"") != std::string::npos,
         "RPC set_reasoning returns enabled reasoning state");
  expect(jsonl.find("\"id\":\"state\"") != std::string::npos && jsonl.find("\"reasoning_level\":\"medium\"") != std::string::npos,
         "RPC get_state reflects selected reasoning");
  expect(jsonl.find("\"id\":\"invalid\"") != std::string::npos && jsonl.find("reasoning level is not supported") != std::string::npos,
         "RPC set_reasoning reports invalid reasoning levels");
  expect(jsonl.find("\"id\":\"set-off\"") != std::string::npos && jsonl.find("\"id\":\"state-off\"") != std::string::npos &&
             jsonl.rfind("\"reasoning_enabled\":false") != std::string::npos,
         "RPC set_reasoning off clears reasoning state instead of storing an active off selection");
  expect(jsonl.find("\"id\":\"clear\"") != std::string::npos && jsonl.rfind("\"reasoning_enabled\":false") != std::string::npos,
         "RPC clear_reasoning disables reasoning state");
  expect(!session_r->reasoning(), "RPC clear_reasoning updates active session state");

  auto entries = session_r->store.load();
  expect(entries.has_value(), "RPC reasoning command test reloads entries");
  if (entries)
  {
    auto const reasoning_changes = std::ranges::count_if(*entries, [](auto const& entry) { return entry.type == ava::session::EntryType::ReasoningChange; });
    expect(reasoning_changes == 2, "RPC reasoning commands persist set and clear reasoning_change entries");
  }
}

void test_app_rpc_reasoning_model_serialization_exposes_resolved_maps()
{
  auto const root = create_empty_root("app-rpc-reasoning-model-serialization");

  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::runtime::OpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC reasoning serialization test opens runtime session");
  if (!session)
    return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({});
  std::istringstream in(
      "{\"id\":\"models\",\"type\":\"list_models\"}\n"
      "{\"id\":\"model\",\"type\":\"set_model\",\"provider\":\"deepseek\",\"model\":\"deepseek-v4-flash\"}\n"
      "{\"id\":\"set\",\"type\":\"set_reasoning\",\"reasoning_level\":\"xhigh\"}\n"
      "{\"id\":\"state\",\"type\":\"get_state\"}\n");
  std::ostringstream out;
  ava::app::runtime::session_ts unlocked_session(std::move(*session));
  auto result =
      ava::app::run_rpc_loop(unlocked_session, open_options, provider, transport, ava::app::runtime::RunOptions{}, in, out, ava::app::rpc::RpcInputWake{});
  ava::app::runtime::session_ts::rat session_r(unlocked_session);
  auto const jsonl = out.str();

  expect(result.has_value(), "RPC reasoning serialization loop completes successfully");
  expect(jsonl.find("\"id\":\"models\"") != std::string::npos && jsonl.find("\"raw_reasoning_levels\"") != std::string::npos &&
             jsonl.find("\"reasoning_level_map\":{") != std::string::npos && jsonl.find("\"xhigh\":\"max\"") != std::string::npos &&
             jsonl.find("\"minimal\":null") != std::string::npos,
         "RPC list_models exposes raw levels plus resolved reasoning map policy");
  expect(jsonl.find("\"id\":\"set\"") != std::string::npos && jsonl.find("\"reasoning_level\":\"xhigh\"") != std::string::npos &&
             jsonl.find("\"reasoning_provider_level\":\"max\"") != std::string::npos && jsonl.find("\"id\":\"state\"") != std::string::npos,
         "RPC get_state exposes provider-level reasoning rewrites when they differ from user-facing levels");

  auto entries = session_r->store.load();
  auto const persisted_provider_level = entries && std::ranges::any_of(*entries, [](ava::session::SessionEntry const& entry) {
                                          return entry.type == ava::session::EntryType::ReasoningChange &&
                                                 entry.data_json.find("\"level\":\"xhigh\"") != std::string::npos &&
                                                 entry.data_json.find("\"provider_level\":\"max\"") != std::string::npos;
                                        });
  expect(persisted_provider_level, "RPC reasoning changes persist provider-level rewrites for replay diagnostics");
}

void test_app_rpc_protocol_version_and_session_commands()
{
  auto const root = create_empty_root("app-rpc-protocol-session");

  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::runtime::OpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC protocol/session test opens runtime session");
  if (!session)
    return;
  auto const initial_id = session->store.session_id();

  ava::session::SessionMetadataUpdate metadata_update;
  metadata_update.name = "Stats audit";
  metadata_update.actor = "test";
  auto appended_metadata = ava::app::append_runtime_session_metadata(*session, std::move(metadata_update));

  auto appended_user = session->append_owned(ava::session::SessionEntry{.id = "entry_user",
                                                                        .parent_id = "",
                                                                        .type = ava::session::EntryType::UserMessage,
                                                                        .timestamp = ava::session::now_timestamp(),
                                                                        .data_json = "{\"text\":\"hello\"}"});
  auto appended_internal_replay = session->append_owned(ava::session::SessionEntry{.id = ava::core::make_id("entry"),
                                                                                   .parent_id = "",
                                                                                   .type = ava::session::EntryType::UserMessage,
                                                                                   .timestamp = ava::session::now_timestamp(),
                                                                                   .data_json = "{\"text\":\"hidden rpc replay\","
                                                                                                "\"internal_replay\":true,"
                                                                                                "\"replay_of\":\"entry_user\","
                                                                                                "\"reason\":\"test\"}"});
  auto appended_assistant = session->append_owned(ava::session::SessionEntry{.id = "entry_assistant",
                                                                             .parent_id = "",
                                                                             .type = ava::session::EntryType::AssistantMessage,
                                                                             .timestamp = ava::session::now_timestamp(),
                                                                             .data_json = "{\"text\":\"answer\",\"usage\":{\"input_tokens\":1,"
                                                                                          "\"output_tokens\":1,\"total_tokens\":2,"
                                                                                          "\"cost_usd\":0.001,"
                                                                                          "\"source\":\"provider\"}}"});
  auto appended_unpriced_assistant = session->append_owned(ava::session::SessionEntry{.id = ava::core::make_id("entry"),
                                                                                      .parent_id = "",
                                                                                      .type = ava::session::EntryType::AssistantMessage,
                                                                                      .timestamp = ava::session::now_timestamp(),
                                                                                      .data_json = "{\"text\":\"unknown cost\",\"usage\":{"
                                                                                                   "\"input_tokens\":1,\"cache_read_tokens\":1,"
                                                                                                   "\"total_tokens\":1,"
                                                                                                   "\"source\":\"provider\"}}"});
  auto appended_reasoning = session->append_owned(ava::session::SessionEntry{.id = ava::core::make_id("entry"),
                                                                             .parent_id = "",
                                                                             .type = ava::session::EntryType::ReasoningBlock,
                                                                             .timestamp = ava::session::now_timestamp(),
                                                                             .data_json = "{\"provider\":\"openai\","
                                                                                          "\"model\":\"gpt-5.5\","
                                                                                          "\"format\":\"openai_responses\","
                                                                                          "\"text\":\"visible reasoning\","
                                                                                          "\"signature\":\"rpc-secret-signature\"}"});
  auto appended_redacted_reasoning = session->append_owned(ava::session::SessionEntry{.id = ava::core::make_id("entry"),
                                                                                      .parent_id = "",
                                                                                      .type = ava::session::EntryType::ReasoningBlock,
                                                                                      .timestamp = ava::session::now_timestamp(),
                                                                                      .data_json = "{\"provider\":\"openai\","
                                                                                                   "\"model\":\"gpt-5.5\","
                                                                                                   "\"format\":\"openai_responses\","
                                                                                                   "\"text\":\"hidden redacted rpc reasoning\","
                                                                                                   "\"signature\":\"rpc-redacted-secret-signature\","
                                                                                                   "\"redacted\": true }"});
  auto appended_mode = session->append_owned(ava::session::SessionEntry{.id = ava::core::make_id("entry"),
                                                                        .parent_id = "",
                                                                        .type = ava::session::EntryType::ModeChange,
                                                                        .timestamp = ava::session::now_timestamp(),
                                                                        .data_json = "{\"mode\":\"build\"}"});
  auto appended_compaction = session->append_owned(ava::session::SessionEntry{.id = ava::core::make_id("entry"),
                                                                              .parent_id = "",
                                                                              .type = ava::session::EntryType::Compaction,
                                                                              .timestamp = ava::session::now_timestamp(),
                                                                              .data_json = "{\"summary\":\"prior\"}"});
  auto appended_cancel = session->append_owned(ava::session::SessionEntry{.id = ava::core::make_id("entry"),
                                                                          .parent_id = "",
                                                                          .type = ava::session::EntryType::Cancel,
                                                                          .timestamp = ava::session::now_timestamp(),
                                                                          .data_json = "{}"});
  auto appended_branch_summary = session->append_owned(ava::session::SessionEntry{.id = ava::core::make_id("entry"),
                                                                                  .parent_id = "",
                                                                                  .type = ava::session::EntryType::BranchSummary,
                                                                                  .timestamp = ava::session::now_timestamp(),
                                                                                  .data_json = "{\"schema_version\":1,\"source_session_id\":\"" + initial_id +
                                                                                               "\",\"branch_root_entry_id\":\"entry_user\"," +
                                                                                               "\"branch_tip_entry_id\":\"entry_assistant\","
                                                                                               "\"summary\":\"branch summary\",\"provider\":\"openai\"," +
                                                                                               "\"model\":\"gpt-test\",\"reason\":\"test\"}"});
  auto v4_text_data = ava::session::serialize_assistant_output_item_data_json(ava::session::AssistantOutputItem{
      .assistant_turn_id = "turn_rpc_projection",
      .sequence = 0,
      .kind = ava::session::AssistantOutputItemKind::Text,
      .provider_item_id = "PRIVATE_RPC_ITEM",
      .provider_output_index = 0,
      .payload = ava::session::AssistantOutputText{.text = "rpc v4 commentary ", .assistant_phase = ava::session::AssistantOutputTextPhase::Commentary}});
  auto v4_reasoning_data = ava::session::serialize_assistant_output_item_data_json(ava::session::AssistantOutputItem{
      .assistant_turn_id = "turn_rpc_projection",
      .sequence = 1,
      .kind = ava::session::AssistantOutputItemKind::Reasoning,
      .provider_item_id = std::nullopt,
      .provider_output_index = std::nullopt,
      .payload = ava::session::AssistantOutputReasoning{.text = "rpc v4 reasoning",
                                                        .format = "openai_responses",
                                                        .redacted = false,
                                                        .signature = "PRIVATE_RPC_SIGNATURE",
                                                        .redacted_data = std::nullopt,
                                                        .native_item_json = "{\"id\":\"PRIVATE_RPC_NATIVE\",\"type\":\"reasoning\",\"summary\":[]}"}});
  auto v4_function_data = ava::session::serialize_assistant_output_item_data_json(ava::session::AssistantOutputItem{
      .assistant_turn_id = "turn_rpc_projection",
      .sequence = 2,
      .kind = ava::session::AssistantOutputItemKind::FunctionCall,
      .provider_item_id = std::nullopt,
      .provider_output_index = std::nullopt,
      .payload =
          ava::session::AssistantOutputFunctionCall{.call_id = "call_rpc_projection", .name = "read_file", .arguments_json = "{\"path\":\"README.md\"}"}});
  auto v4_commit_data = ava::session::serialize_assistant_turn_commit_data_json(ava::session::AssistantTurnCommit{.assistant_turn_id = "turn_rpc_projection",
                                                                                                                  .item_count = 3,
                                                                                                                  .provider = "openai",
                                                                                                                  .model = "gpt-5.5",
                                                                                                                  .finish_reason = "tool_calls",
                                                                                                                  .usage_json = std::nullopt});
  auto appended_v4_text = v4_text_data && session->append_owned(ava::session::SessionEntry{.id = "rpc_v4_text",
                                                                                           .parent_id = "",
                                                                                           .type = ava::session::EntryType::AssistantOutputItem,
                                                                                           .timestamp = ava::session::now_timestamp(),
                                                                                           .data_json = *v4_text_data});
  auto appended_v4_reasoning = v4_reasoning_data && session->append_owned(ava::session::SessionEntry{.id = "rpc_v4_reasoning",
                                                                                                     .parent_id = "",
                                                                                                     .type = ava::session::EntryType::AssistantOutputItem,
                                                                                                     .timestamp = ava::session::now_timestamp(),
                                                                                                     .data_json = *v4_reasoning_data});
  auto appended_v4_function = v4_function_data && session->append_owned(ava::session::SessionEntry{.id = "rpc_v4_function",
                                                                                                   .parent_id = "",
                                                                                                   .type = ava::session::EntryType::AssistantOutputItem,
                                                                                                   .timestamp = ava::session::now_timestamp(),
                                                                                                   .data_json = *v4_function_data});
  auto appended_v4_commit = v4_commit_data && session->append_owned(ava::session::SessionEntry{.id = "rpc_v4_commit",
                                                                                               .parent_id = "",
                                                                                               .type = ava::session::EntryType::AssistantTurnCommit,
                                                                                               .timestamp = ava::session::now_timestamp(),
                                                                                               .data_json = *v4_commit_data});
  auto appended_v4_result =
      session->append_owned(ava::session::SessionEntry{.id = "rpc_v4_result",
                                                       .parent_id = "",
                                                       .type = ava::session::EntryType::ToolResult,
                                                       .timestamp = ava::session::now_timestamp(),
                                                       .data_json = "{\"assistant_output_entry_id\":\"rpc_v4_function\",\"call_id\":\"call_rpc_projection\","
                                                                    "\"name\":\"read_file\",\"success\":true,\"result\":\"rpc tool result\"}"});
  expect(appended_metadata.has_value() && appended_user.has_value() && appended_internal_replay.has_value() && appended_assistant.has_value() &&
             appended_unpriced_assistant.has_value() && appended_reasoning.has_value() && appended_redacted_reasoning.has_value() &&
             appended_mode.has_value() && appended_compaction.has_value() && appended_cancel.has_value() && appended_branch_summary.has_value() &&
             appended_v4_text && appended_v4_reasoning && appended_v4_function && appended_v4_commit && appended_v4_result.has_value(),
         "RPC protocol/session test appends legacy and committed v4 message history");

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({});
  std::istringstream in(
      "{\"id\":\"proto\",\"type\":\"get_protocol\",\"protocol_version\":1}\n"
      "{\"id\":\"messages\",\"type\":\"get_messages\"}\n"
      "{\"id\":\"stats\",\"type\":\"get_session_stats\"}\n"
      "{\"id\":\"validate\",\"type\":\"validate_session\"}\n"
      "{\"id\":\"new\",\"type\":\"new_session\"}\n"
      "{\"id\":\"switch\",\"type\":\"switch_session\",\"session_id\":\"" +
      initial_id + "\"}\n");
  std::ostringstream out;
  ava::app::runtime::RunOptions runtime_options;
  runtime_options.access_token = "token";
  ava::app::runtime::session_ts unlocked_session(std::move(*session));
  auto result = ava::app::run_rpc_loop(unlocked_session, open_options, provider, transport, runtime_options, in, out, ava::app::rpc::RpcInputWake{});
  auto const jsonl = out.str();
  auto const current_entry_version = std::string("\"version\":") + std::to_string(ava::session::kCurrentSessionEntryVersion);
  expect(result.has_value(), "RPC protocol/session loop completes successfully");
  expect(jsonl.find("\"id\":\"proto\"") != std::string::npos && jsonl.find("\"protocol_version\":1") != std::string::npos &&
             jsonl.find("\"supported_protocol_versions\":[1]") != std::string::npos &&
             jsonl.find("\"session_entry_version\":" + std::to_string(ava::session::kCurrentSessionEntryVersion)) != std::string::npos &&
             jsonl.find("\"supported_session_entry_versions\":[0,1,2,3,4]") != std::string::npos &&
             jsonl.find("\"capabilities\":[\"direct_bash_rpc\",\"job_controls\"]") != std::string::npos &&
             jsonl.find("\"direct_command_types\":[\"run_bash\",\"run_command\",\"list_jobs\"") != std::string::npos,
         "RPC get_protocol reports supported protocol, session entry versions, and direct job capabilities");
  expect(jsonl.find("\"id\":\"messages\"") != std::string::npos && jsonl.find("\"messages\"") != std::string::npos &&
             jsonl.find(current_entry_version) != std::string::npos && jsonl.find("hello") != std::string::npos && jsonl.find("answer") != std::string::npos &&
             jsonl.find("visible reasoning") != std::string::npos && jsonl.find("hidden redacted rpc reasoning") == std::string::npos &&
             jsonl.find("rpc v4 commentary ") != std::string::npos && jsonl.find("rpc v4 reasoning") != std::string::npos &&
             jsonl.find("call_rpc_projection") != std::string::npos && jsonl.find("rpc tool result") != std::string::npos &&
             jsonl.find("\"ordered_output\":[{\"sequence\":0,\"kind\":\"text\",\"text\":\"rpc v4 commentary \",\"assistant_phase\":\"commentary\"}") !=
                 std::string::npos &&
             jsonl.find("\"sequence\":1,\"kind\":\"reasoning\"") != std::string::npos &&
             jsonl.find("\"sequence\":2,\"kind\":\"function_call\"") != std::string::npos && jsonl.find("\"signature_present\":true") != std::string::npos &&
             jsonl.find("hidden rpc replay") == std::string::npos && jsonl.find("rpc-secret-signature") == std::string::npos &&
             jsonl.find("rpc-redacted-secret-signature") == std::string::npos && jsonl.find("PRIVATE_RPC_ITEM") == std::string::npos &&
             jsonl.find("PRIVATE_RPC_SIGNATURE") == std::string::npos && jsonl.find("PRIVATE_RPC_NATIVE") == std::string::npos &&
             jsonl.find("assistant_output_entry_id") == std::string::npos,
         "RPC get_messages returns projected committed v4 content without private replay metadata or exact bindings");
  expect(jsonl.find("\"id\":\"stats\"") != std::string::npos && jsonl.find("\"entry_count\":16") != std::string::npos &&
             jsonl.find("\"session_metadata\":1") != std::string::npos && jsonl.find("\"user_message\":1") != std::string::npos &&
             jsonl.find("\"assistant_message\":3") != std::string::npos && jsonl.find("\"reasoning_block\":3") != std::string::npos &&
             jsonl.find("\"tool_call\":1") != std::string::npos && jsonl.find("\"tool_result\":1") != std::string::npos &&
             jsonl.find("\"mode_change\":1") != std::string::npos && jsonl.find("\"compaction\":1") != std::string::npos &&
             jsonl.find("\"branch_summary\":1") != std::string::npos && jsonl.find("\"cancel\":1") != std::string::npos,
         "RPC get_session_stats returns session counters");
  expect(jsonl.find("\"known_cost_usd\":0.001") != std::string::npos && jsonl.find("\"cost_complete\":false") != std::string::npos &&
             jsonl.find("\"unknown_cost_entries\":1") != std::string::npos && jsonl.find("\"total_cost_usd\"") == std::string::npos,
         "RPC get_session_stats omits incomplete total cost and reports known cost metadata");
  expect(jsonl.find("\"id\":\"validate\"") != std::string::npos && jsonl.find("\"ok\":true") != std::string::npos &&
             jsonl.find("\"error_count\":0") != std::string::npos,
         "RPC validate_session reports a clean replay audit for the active session");
  expect(jsonl.find("\"id\":\"new\"") != std::string::npos && jsonl.find("\"created\":true") != std::string::npos,
         "RPC new_session creates and switches to a new active session");
  ava::app::runtime::session_ts::rat session_r(unlocked_session);
  expect(session_r->store.session_id() == initial_id && jsonl.find("\"id\":\"switch\"") != std::string::npos,
         "RPC switch_session switches back to the requested session");
}

void test_app_rpc_messages_keep_v1_payloads_when_ordered_output_does_not_fit()
{
  auto const root = create_empty_root("app-rpc-messages-ordered-output-caps");
  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  std::filesystem::permissions(root, std::filesystem::perms::owner_all);

  auto open_session = [&](std::string_view name) {
    ava::app::runtime::OpenOptions options;
    options.workspace_dir = workspace / std::string(name);
    options.current_dir = options.workspace_dir;
    options.paths = app_test_paths(root / std::string(name));
    std::filesystem::create_directories(options.workspace_dir);
    return ava::app::open_runtime_session(options);
  };
  auto append_v4_text_turn = [](ava::app::runtime::Session& session, std::size_t index, std::string text) {
    auto const turn_id = "rpc_cap_turn_" + std::to_string(index);
    auto const item_id = "rpc_cap_item_" + std::to_string(index);
    auto item_data = ava::session::serialize_assistant_output_item_data_json(ava::session::AssistantOutputItem{
        .assistant_turn_id = turn_id,
        .sequence = 0,
        .kind = ava::session::AssistantOutputItemKind::Text,
        .provider_item_id = "PRIVATE_RPC_CAP_ITEM_" + std::to_string(index),
        .provider_output_index = 0,
        .payload = ava::session::AssistantOutputText{.text = std::move(text), .assistant_phase = ava::session::AssistantOutputTextPhase::Commentary}});
    auto commit_data = ava::session::serialize_assistant_turn_commit_data_json(ava::session::AssistantTurnCommit{
        .assistant_turn_id = turn_id, .item_count = 1, .provider = "openai", .model = "gpt-5.5", .finish_reason = "completed", .usage_json = std::nullopt});
    if (!item_data || !commit_data)
      return false;
    auto item = session.append_owned(ava::session::SessionEntry{.id = item_id,
                                                                .parent_id = "",
                                                                .type = ava::session::EntryType::AssistantOutputItem,
                                                                .timestamp = ava::session::now_timestamp(),
                                                                .data_json = std::move(*item_data)});
    auto commit = session.append_owned(ava::session::SessionEntry{.id = "rpc_cap_commit_" + std::to_string(index),
                                                                  .parent_id = item_id,
                                                                  .type = ava::session::EntryType::AssistantTurnCommit,
                                                                  .timestamp = ava::session::now_timestamp(),
                                                                  .data_json = std::move(*commit_data)});
    return item.has_value() && commit.has_value();
  };

  auto single_session = open_session("single");
  bool const appended_single = single_session && append_v4_text_turn(*single_session, 0, "RPC_SINGLE_" + std::string(6'000, 's'));
  std::optional<std::string> single_json;
  if (appended_single)
  {
    auto serialized = ava::app::rpc::messages_result_json(*single_session);
    if (serialized)
      single_json = std::move(*serialized);
  }
  expect(single_json && single_json->find("RPC_SINGLE_") != std::string::npos && single_json->find("\"tool_calls\":0") != std::string::npos &&
             single_json->find("\"ordered_output\"") == std::string::npos && single_json->find("\"message_count\":1") != std::string::npos &&
             single_json->find("\"ordered_output_truncated\":true") != std::string::npos && single_json->find("PRIVATE_RPC_CAP_ITEM") == std::string::npos,
         "RPC keeps a 5--8 KiB v1 assistant payload when additive ordered output exceeds its per-entry cap");

  auto near_cap_session = open_session("near-cap");
  bool appended_near_cap = near_cap_session.has_value();
  for (std::size_t index = 0; appended_near_cap && index < 190; ++index)
    appended_near_cap = append_v4_text_turn(*near_cap_session, index, "RPC_CAP_" + std::to_string(index) + "_" + std::string(6'000, 'n'));
  std::optional<std::string> near_cap_json;
  if (appended_near_cap)
  {
    auto serialized = ava::app::rpc::messages_result_json(*near_cap_session);
    if (serialized)
      near_cap_json = std::move(*serialized);
  }
  auto const near_cap_count = near_cap_json ? ava::core::json::integer_field(*near_cap_json, "message_count") : std::nullopt;
  expect(near_cap_json && near_cap_json->size() <= ava::app::rpc::kMaxRpcMessagesResponseBytes && near_cap_count && *near_cap_count > 150 &&
             near_cap_json->find("RPC_CAP_150_") != std::string::npos && near_cap_json->find("\"original_bytes\"") == std::string::npos &&
             near_cap_json->find("\"ordered_output_truncated\":true") != std::string::npos && near_cap_json->find("PRIVATE_RPC_CAP_ITEM") == std::string::npos,
         "near-cap RPC histories retain their legacy message count and data while reporting omitted ordered detail");
}

void test_app_rpc_protocol_version_and_resolver_reply_errors()
{
  auto const root = create_empty_root("app-rpc-protocol-errors");

  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::runtime::OpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC protocol error test opens runtime session");
  if (!session)
    return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({});
  std::string input =
      "{\"id\":\"bad-version\",\"type\":\"get_state\",\"protocol_version\":999}\n"
      "{\"id\":\"reply-missing\",\"type\":\"permission_reply\"}\n"
      "{\"id\":\"bad-version-type\",\"type\":\"get_state\",\"protocol_version\":\"1\"}\n"
      "{\"id\":\"oversized-reply\",\"type\":\"permission_reply\",\"request_id\":\"" +
      std::string(257, 'r') +
      "\",\"correlation_id\":\"p1\",\"decision\":\"allow\"}\n"
      "{\"id\":\"reply\",\"type\":\"question_reply\",\"request_id\":\"question_1\",\"correlation_id\":\"p1\","
      "\"answer\":\"ok\"}\n"
      "{\"id\":\"state\",\"type\":\"get_state\",\"protocol_version\":1}\n";
  std::istringstream in(input);
  std::ostringstream out;
  ava::app::runtime::session_ts unlocked_session(std::move(*session));
  auto result =
      ava::app::run_rpc_loop(unlocked_session, open_options, provider, transport, ava::app::runtime::RunOptions{}, in, out, ava::app::rpc::RpcInputWake{});
  auto const jsonl = out.str();
  expect(result.has_value(), "RPC protocol error loop recovers after unsupported commands");
  expect(jsonl.find("unsupported RPC protocol version") != std::string::npos && jsonl.find("RPC protocol_version must be an integer") != std::string::npos &&
             jsonl.find("permission_reply requires request_id") != std::string::npos && jsonl.find("\"id\":\"oversized-reply\"") != std::string::npos &&
             jsonl.find("RPC identifier is too long") != std::string::npos &&
             jsonl.find("RPC resolver reply has no matching pending request") != std::string::npos && jsonl.find("\"id\":\"state\"") != std::string::npos &&
             jsonl.find("\"success\":true") != std::string::npos,
         "RPC version and resolver reply errors are in-band and recoverable");
}

void test_app_rpc_mcp_command_responses()
{
  expect(!std::string_view(AVA_FAKE_MCP_SERVER_PATH).empty(), "RPC MCP command test has fake server path");
  if (std::string_view(AVA_FAKE_MCP_SERVER_PATH).empty())
    return;

  auto const root = create_empty_root("app-rpc-mcp-commands");

  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  write_app_test_file(workspace / ".ava" / "mcp.json", app_test_mcp_config_json("demo", "Demo MCP", AVA_FAKE_MCP_SERVER_PATH));
  auto trusted = ava::app::set_project_trust_decision(paths, workspace, true);
  expect(trusted.has_value(),
         trusted ? "RPC MCP command test trusts project config" : "RPC MCP command test trusts project config: " + trusted.error().format());

  ava::app::runtime::OpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC MCP command test opens runtime session");
  if (!session)
    return;

  std::vector<ava::permissions::PermissionPrompt> prompts;
  ava::app::runtime::RunOptions runtime_options;
  runtime_options.permission_resolver =
      [&prompts](ava::permissions::PermissionPrompt const& prompt) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
    prompts.push_back(prompt);
    return ava::permissions::PermissionResolution::Allow;
  };

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({});
  std::istringstream in(
      "{\"id\":\"mcp-list\",\"type\":\"list_mcp_servers\"}\n"
      "{\"id\":\"mcp-inspect\",\"type\":\"inspect_mcp_server\",\"server_id\":\"demo\"}\n"
      "{\"id\":\"mcp-tools\",\"type\":\"list_mcp_tools\",\"server_id\":\"demo\"}\n"
      "{\"id\":\"mcp-restart\",\"type\":\"restart_mcp_server\",\"server_id\":\"demo\"}\n");
  std::ostringstream out;
  ava::app::runtime::session_ts unlocked_session(std::move(*session));
  auto result = ava::app::run_rpc_loop(unlocked_session, open_options, provider, transport, runtime_options, in, out, ava::app::rpc::RpcInputWake{});
  auto const jsonl = out.str();

  auto const has_id = [&jsonl](std::string_view id) { return jsonl.find("\"id\":\"" + std::string(id) + "\"") != std::string::npos; };
  expect(result.has_value(), "RPC MCP command loop completes successfully");
  expect(has_id("mcp-list") && has_id("mcp-inspect") && has_id("mcp-tools") && has_id("mcp-restart"), "RPC MCP command responses include all request ids");
  expect(jsonl.find("MCP servers:") != std::string::npos && jsonl.find("Demo MCP") != std::string::npos && jsonl.find("MCP server demo") != std::string::npos &&
             jsonl.find("MCP tools for demo") != std::string::npos && jsonl.find("fake-mcp") != std::string::npos && jsonl.find("echo") != std::string::npos &&
             jsonl.find("mcp_demo_echo") != std::string::npos && jsonl.find("next discovery or tool call will launch a fresh process") != std::string::npos,
         "RPC MCP command responses expose list, inspect, tools, and restart output");

  auto const has_launch_prompt = std::ranges::any_of(
      prompts, [](auto const& prompt) { return prompt.operation == ava::permissions::Operation::McpServerLaunch && prompt.tool_name == "mcp_tools"; });
  auto const has_connect_prompt = std::ranges::any_of(prompts, [](auto const& prompt) {
    return prompt.operation == ava::permissions::Operation::McpServerConnect && prompt.tool_name == "mcp_tools" && prompt.command == "demo";
  });
  expect(has_launch_prompt && has_connect_prompt, "RPC list_mcp_tools requests MCP launch and connect permissions before allowing discovery");
}

void test_app_rpc_command_responses_for_context_compact_export()
{
  auto const root = create_empty_root("app-rpc-commands");

  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  {
    std::ofstream file(workspace / "AGENTS.md", std::ios::binary | std::ios::trunc);
    file << "rpc command context\n";
  }
  write_app_test_file(workspace / ".ava" / "plugins" / "com.example.rpc" / "plugin.json", app_test_plugin_manifest_json("com.example.rpc", "RPC Plugin"));
  write_app_test_file(workspace / ".ava" / "plugins" / "com.example.rpcbad" / "plugin.json", "{not-json");
  auto const rpc_install_source = root / "rpc-install-source";
  write_app_test_file(rpc_install_source / "plugin.json", app_test_plugin_manifest_json("com.example.rpcinstall", "RPC Installed Plugin"));
  auto trusted = ava::app::set_project_trust_decision(paths, workspace, true);
  expect(trusted.has_value(),
         trusted ? "RPC command test trusts project plugin resources" : "RPC command test trusts project plugin resources: " + trusted.error().format());

  ava::app::runtime::OpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC command test opens runtime session");
  if (!session)
    return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  std::string const rpc_summary =
      "# Goal\nRemember RPC facts\n# Constraints / Preferences\nNone noted.\n# Decisions\nNone noted.\n"
      "# Files Read or Modified\nNone noted.\n# Unresolved Tasks\nNone noted.\n# Next Steps\nContinue.";
  ava::tests::FakeTransport transport(
      {ava::provider::HttpResponse{.status_code = 200, .headers = {}, .body = "{\"output_text\":\"" + ava::core::json::escape(rpc_summary) + "\"}"}});
  BlockingInputBuf input_buffer;
  std::istream in(&input_buffer);
  ThreadSafeStringBuf output_buffer;
  std::ostream out(&output_buffer);
  ava::app::runtime::RunOptions runtime_options;
  runtime_options.access_token = "token";
  runtime_options.permission_resolver = [](ava::permissions::PermissionPrompt const&) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
    return ava::permissions::PermissionResolutionDecision{ava::permissions::PermissionResolution::Allow, "test export write"};
  };
  ava::core::VoidResult result;
  ava::app::runtime::session_ts unlocked_session(std::move(*session));
  std::jthread rpc_thread([&] {
    result = ava::app::run_rpc_loop(unlocked_session, open_options, provider, transport, runtime_options, in, out, [&] noexcept { input_buffer.close(); });
  });
  input_buffer.push(std::string("{\"id\":\"plugins\",\"type\":\"list_plugins\"}\n") +
                    "{\"id\":\"plugin-enable\",\"type\":\"enable_plugin\",\"plugin_id\":\"com.example.rpc\"}\n"
                    "{\"id\":\"plugin-inspect\",\"type\":\"inspect_plugin\",\"plugin_id\":\"com.example.rpc\"}\n"
                    "{\"id\":\"plugin-validate\",\"type\":\"validate_plugin\",\"path\":\".ava/plugins/com.example.rpc/plugin.json\"}\n" +
                    "{\"id\":\"plugin-install\",\"type\":\"install_plugin\",\"path\":\"" + ava::core::json::escape(rpc_install_source.generic_string()) +
                    "\"}\n"
                    "{\"id\":\"plugin-remove\",\"type\":\"remove_plugin\",\"plugin_id\":\"com.example.rpcinstall\"}\n"
                    "{\"id\":\"plugin-failures\",\"type\":\"plugin_failures\"}\n"
                    "{\"id\":\"ctx\",\"type\":\"context\"}\n"
                    "{\"id\":\"read\",\"type\":\"invoke_command\",\"name\":\"read\",\"command_arguments\":\"AGENTS.md\"}\n"
                    "{\"id\":\"cmp\",\"type\":\"compact\",\"instructions\":\"remember rpc facts\"}\n");
  bool const compacted = output_buffer.wait_contains("compaction summary recorded", std::chrono::seconds(2));
  input_buffer.push(
      "{\"id\":\"exp\",\"type\":\"export\"}\n"
      "{\"id\":\"exp-html\",\"type\":\"export_html\"}\n"
      "{\"id\":\"exp-html-file\",\"type\":\"export_html\",\"outputPath\":\"rpc-session.html\"}\n");
  bool const exported = output_buffer.wait_contains("\"id\":\"exp-html-file\"", std::chrono::seconds(2));
  input_buffer.close();
  rpc_thread.join();
  auto const jsonl = output_buffer.str();
  std::ifstream exported_html_file(workspace / "rpc-session.html", std::ios::binary);
  std::ostringstream exported_html_text;
  exported_html_text << exported_html_file.rdbuf();
  expect(result.has_value() && compacted && exported, "RPC context/compact/export loop completes successfully");
  expect(jsonl.find("\"id\":\"plugins\"") != std::string::npos && jsonl.find("com.example.rpc") != std::string::npos &&
             jsonl.find("\"id\":\"plugin-enable\"") != std::string::npos && jsonl.find("No plugin process was started") != std::string::npos &&
             jsonl.find("\"id\":\"plugin-inspect\"") != std::string::npos && jsonl.find("status: enabled") != std::string::npos &&
             jsonl.find("\"id\":\"plugin-validate\"") != std::string::npos && jsonl.find("Valid plugin manifest") != std::string::npos &&
             jsonl.find("\"id\":\"plugin-install\"") != std::string::npos &&
             jsonl.find("Installed global plugin com.example.rpcinstall") != std::string::npos && jsonl.find("\"id\":\"plugin-remove\"") != std::string::npos &&
             jsonl.find("Removed global plugin com.example.rpcinstall") != std::string::npos &&
             !std::filesystem::exists(paths.ava_config_dir / "plugins" / "com.example.rpcinstall") &&
             jsonl.find("\"id\":\"plugin-failures\"") != std::string::npos && jsonl.find("com.example.rpcbad") != std::string::npos &&
             jsonl.find("\"id\":\"ctx\"") != std::string::npos && jsonl.find("AGENTS.md") != std::string::npos &&
             jsonl.find("\"id\":\"read\"") != std::string::npos && jsonl.find("\"tool_timeline\"") != std::string::npos &&
             jsonl.find("\"structured_result\":{\"schema_version\":1") != std::string::npos && jsonl.find("\"tool\":\"read\"") != std::string::npos &&
             jsonl.find("\"id\":\"cmp\"") != std::string::npos && jsonl.find("\"name\":\"compaction_start\"") != std::string::npos &&
             jsonl.find("\"name\":\"compaction_end\"") != std::string::npos && jsonl.find("compaction summary recorded") != std::string::npos &&
             jsonl.find("\"id\":\"exp\"") != std::string::npos && jsonl.find("# AVA Session Export") != std::string::npos &&
             jsonl.find("remember rpc facts") != std::string::npos && jsonl.find("\"id\":\"exp-html\"") != std::string::npos &&
             jsonl.find("<!doctype html>") != std::string::npos && jsonl.find("\"id\":\"exp-html-file\"") != std::string::npos &&
             jsonl.find("format: html") != std::string::npos && exported_html_text.str().find("<!doctype html>") != std::string::npos &&
             exported_html_text.str().find("# AVA Session Export") != std::string::npos,
         "RPC command responses expose dispatcher output plus Pi-style export_html records");
}

void test_app_rpc_direct_run_command_permission_reply_executes_and_audits()
{
  auto const root = create_empty_root("app-rpc-direct-run-command");

  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  expect(::chmod(root.c_str(), S_IRWXU) == 0 && ::chmod(workspace.c_str(), S_IRWXU) == 0,
         "RPC direct command fixture workspace is owner-only for sealed command planning");

  ava::app::runtime::OpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC direct command test opens runtime session");
  if (!session)
    return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({});
  BlockingInputBuf input_buffer;
  std::istream in(&input_buffer);
  ThreadSafeStringBuf output_buffer;
  std::ostream out(&output_buffer);
  ava::core::VoidResult result;
  ava::app::runtime::session_ts unlocked_session(std::move(*session));
  std::jthread rpc_thread([&] {
    result = ava::app::run_rpc_loop(unlocked_session, open_options, provider, transport, ava::app::runtime::RunOptions{}, in, out,
                                    [&] noexcept { input_buffer.close(); });
  });

  input_buffer.push(R"JSON({"id":"cmd-allow","type":"run_command","command":"printf rpc-direct"})JSON"
                    "\n");
  bool const permission_requested = output_buffer.wait_contains("\"name\":\"permission_requested\"", std::chrono::seconds(2));
  auto const request_id = rpc_string_field_from_output(output_buffer.str(), "resolver_request_id");
  expect(permission_requested && request_id, "RPC direct command emits an Operation::RunCommand permission request before execution");
  if (request_id)
  {
    input_buffer.push("{\"id\":\"allow\",\"type\":\"permission_reply\",\"request_id\":\"" + *request_id +
                      "\",\"correlation_id\":\"cmd-allow\",\"decision\":\"allow\",\"reason\":\"approved direct rpc\"}\n");
  }
  bool const completed = output_buffer.wait_contains("\"output\":\"rpc-direct\"", std::chrono::seconds(2));
  input_buffer.close();
  rpc_thread.join();

  ava::app::runtime::session_ts::rat session_r(unlocked_session);
  auto const jsonl = output_buffer.str();
  auto entries = session_r->store.load();
  auto const audited_allow = entries && std::ranges::any_of(*entries, [](ava::session::SessionEntry const& entry) {
                               return entry.type == ava::session::EntryType::PermissionDecision &&
                                      entry.data_json.find("\"operation\":\"bash\"") != std::string::npos &&
                                      entry.data_json.find("\"command\":\"<redacted one-shot command>\"") != std::string::npos &&
                                      entry.data_json.find("\"resolution\":\"allow\"") != std::string::npos;
                             });
  expect(result.has_value(), "RPC direct command loop completes successfully");
  expect(transport.requests().empty(), "RPC direct command execution does not dispatch provider requests");
  expect(completed && jsonl.find("\"id\":\"cmd-allow\"") != std::string::npos && jsonl.find("\"success\":true") != std::string::npos &&
             jsonl.find("\"operation\":\"bash\"") != std::string::npos && jsonl.find("\"command\":\"printf rpc-direct\"") != std::string::npos &&
             jsonl.find("\"family\":\"raw_shell\"") != std::string::npos && jsonl.find("\"backend_maximum_scope\":\"once\"") != std::string::npos &&
             jsonl.find("\"tool\":\"bash\"") != std::string::npos && jsonl.find("\"permission_request_ids\":[\"permreq_") != std::string::npos,
         "RPC direct command is a one-shot raw-shell bash operation linked to its permission request");
  expect(audited_allow, "RPC direct command persists permission audit decisions in the session");
}

void test_app_rpc_direct_run_command_permission_denial_blocks_execution()
{
  auto const root = create_empty_root("app-rpc-direct-run-command-deny");

  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  expect(::chmod(root.c_str(), S_IRWXU) == 0 && ::chmod(workspace.c_str(), S_IRWXU) == 0,
         "RPC direct command denial fixture workspace is owner-only for sealed command planning");

  ava::app::runtime::OpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC direct command denial test opens runtime session");
  if (!session)
    return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({});
  BlockingInputBuf input_buffer;
  std::istream in(&input_buffer);
  ThreadSafeStringBuf output_buffer;
  std::ostream out(&output_buffer);
  ava::core::VoidResult result;
  ava::app::runtime::session_ts unlocked_session(std::move(*session));
  std::jthread rpc_thread([&] {
    result = ava::app::run_rpc_loop(unlocked_session, open_options, provider, transport, ava::app::runtime::RunOptions{}, in, out,
                                    [&] noexcept { input_buffer.close(); });
  });

  std::string const secret = "RPC_DENIED_COMMAND_SECRET_SENTINEL";
  input_buffer.push("{\"id\":\"cmd-deny\",\"type\":\"run_bash\",\"command\":\"printf " + secret + "\"}\n");
  bool const permission_requested = output_buffer.wait_contains("\"name\":\"permission_requested\"", std::chrono::seconds(2));
  auto const request_id = rpc_string_field_from_output(output_buffer.str(), "resolver_request_id");
  expect(permission_requested && request_id && output_buffer.str().find(secret) != std::string::npos,
         "RPC direct command denial emits a permission prompt that retains the authorized user's exact command");
  if (request_id)
  {
    input_buffer.push("{\"id\":\"deny\",\"type\":\"permission_reply\",\"request_id\":\"" + *request_id +
                      "\",\"correlation_id\":\"cmd-deny\",\"decision\":\"deny\",\"reason\":\"not approved\"}\n");
  }
  bool const denied = output_buffer.wait_contains("command requires permission", std::chrono::seconds(2));
  input_buffer.close();
  rpc_thread.join();

  ava::app::runtime::session_ts::rat session_r(unlocked_session);
  auto const jsonl = output_buffer.str();
  auto entries = session_r->store.load();
  auto const audited_deny = entries && std::ranges::any_of(*entries, [](ava::session::SessionEntry const& entry) {
                              return entry.type == ava::session::EntryType::PermissionDecision &&
                                     entry.data_json.find("\"operation\":\"bash\"") != std::string::npos &&
                                     entry.data_json.find("\"command\":\"<redacted one-shot command>\"") != std::string::npos &&
                                     entry.data_json.find("\"resolution\":\"deny\"") != std::string::npos;
                            });
  expect(result.has_value(), "RPC direct command denial loop completes successfully");
  auto const session_secret_absent =
      entries && std::ranges::all_of(*entries, [&](ava::session::SessionEntry const& entry) { return entry.data_json.find(secret) == std::string::npos; });
  expect(denied && jsonl.find("\"id\":\"cmd-deny\"") != std::string::npos && jsonl.find("\"tool\":\"bash\"") != std::string::npos &&
             count_substrings(jsonl, secret) == 1,
         "RPC direct command denial keeps the argument only in its permission prompt, not its reply diagnostics");
  expect(!std::filesystem::exists(workspace / "denied.txt"), "RPC direct command denial blocks process execution before side effects");
  expect(audited_deny && session_secret_absent, "RPC direct command denial persists redacted session audits without command arguments");
}

void test_app_rpc_direct_run_command_active_rejects_and_cancels_process()
{
  auto const root = create_empty_root("app-rpc-direct-run-command-cancel");

  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  expect(::chmod(root.c_str(), S_IRWXU) == 0 && ::chmod(workspace.c_str(), S_IRWXU) == 0,
         "RPC direct command cancellation fixture workspace is owner-only for sealed command planning");

  ava::app::runtime::OpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC direct command cancellation test opens runtime session");
  if (!session)
    return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({});
  BlockingInputBuf input_buffer;
  std::istream in(&input_buffer);
  ThreadSafeStringBuf output_buffer;
  std::ostream out(&output_buffer);
  ava::core::VoidResult result;
  ava::app::runtime::RunOptions runtime_options;
  runtime_options.permission_resolver = [](ava::permissions::PermissionPrompt const&) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
    return ava::permissions::PermissionResolution::Allow;
  };
  ava::app::runtime::session_ts unlocked_session(std::move(*session));
  std::jthread rpc_thread([&] {
    result = ava::app::run_rpc_loop(unlocked_session, open_options, provider, transport, runtime_options, in, out, [&] noexcept { input_buffer.close(); });
  });

  auto const sleep_marker = workspace / "sleep-started";
  auto const sleep_command = "/bin/sh -c 'touch " + sleep_marker.string() + "; sleep 5'";
  input_buffer.push("{\"id\":\"cmd-sleep\",\"type\":\"run_command\",\"command\":\"" + ava::core::json::escape(sleep_command) + "\"}\n");
  bool const started = output_buffer.wait_contains("\"name\":\"tool_start\"", std::chrono::seconds(2));
  auto const launch_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (!std::filesystem::exists(sleep_marker) && std::chrono::steady_clock::now() < launch_deadline)
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  bool const process_started = std::filesystem::exists(sleep_marker);
  input_buffer.push(R"JSON({"id":"cmd-second","type":"run_bash","command":"printf should-not-run"})JSON"
                    "\n");
  bool const active_rejected = output_buffer.wait_contains("RPC command is unavailable while a prompt is active", std::chrono::seconds(2));
  input_buffer.push(R"JSON({"id":"cancel-sleep","type":"cancel"})JSON"
                    "\n");
  bool const canceled = output_buffer.wait_contains("\"canceled\":true", std::chrono::seconds(2));
  input_buffer.close();
  rpc_thread.join();

  auto const jsonl = output_buffer.str();
  expect(result.has_value(), "RPC direct command cancellation loop completes successfully");
  expect(transport.requests().empty(), "RPC direct command cancellation does not dispatch provider requests");
  expect(started && process_started && active_rejected && canceled && jsonl.find("\"id\":\"cmd-second\"") != std::string::npos &&
             jsonl.find("should-not-run") == std::string::npos && jsonl.find("\"id\":\"cmd-sleep\"") != std::string::npos,
         "RPC direct command rejects concurrent commands and cancels the running process through the bash tool context");
}

void test_app_rpc_compact_provider_failure_is_error_response()
{
  auto const root = create_empty_root("app-rpc-compact-failure");

  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::runtime::OpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC compact failure test opens runtime session");
  if (!session)
    return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({ava::provider::HttpResponse{.status_code = 500, .headers = {}, .body = "{\"error\":{\"message\":\"summary failed\"}}"}});
  BlockingInputBuf input_buffer;
  std::istream in(&input_buffer);
  ThreadSafeStringBuf output_buffer;
  std::ostream out(&output_buffer);
  ava::app::runtime::RunOptions runtime_options;
  runtime_options.access_token = "token";

  ava::core::VoidResult result;
  ava::app::runtime::session_ts unlocked_session(std::move(*session));
  std::jthread rpc_thread([&] {
    result = ava::app::run_rpc_loop(unlocked_session, open_options, provider, transport, runtime_options, in, out, [&] noexcept { input_buffer.close(); });
  });
  input_buffer.push("{\"id\":\"cmp-fail\",\"type\":\"compact\"}\n");
  bool const failed = output_buffer.wait_contains("compaction summary request failed with status 500", std::chrono::seconds(2));
  input_buffer.close();
  rpc_thread.join();
  ava::app::runtime::session_ts::rat session_r(unlocked_session);
  auto const jsonl = output_buffer.str();
  auto entries = session_r->store.load();
  expect(result.has_value() && failed, "RPC compact failure loop completes after error response");
  expect(jsonl.find("\"id\":\"cmp-fail\"") != std::string::npos && jsonl.find("\"success\":false") != std::string::npos &&
             jsonl.find("compaction summary request failed with status 500") != std::string::npos && jsonl.find("summary failed") == std::string::npos,
         "RPC compact provider failures are machine-readable responses without provider-controlled diagnostics");
  expect(entries && count_compaction_entries(*entries) == 0, "RPC compact provider failure leaves session without a compaction entry");
}

void test_app_rpc_production_catalog_and_golden_contract()
{
  auto const golden = std::filesystem::path(AVA_RPC_V1_GOLDEN_DIR);
  auto const manifest = read_rpc_golden(golden / "manifest.json");
  auto const manifest_commands = ava::core::json::strings_in_array_field(manifest, "request_types");
  auto const manifest_events = ava::core::json::strings_in_array_field(manifest, "event_names");
  auto const manifest_errors = ava::core::json::strings_in_array_field(manifest, "stable_error_codes");
  auto as_set = [](auto const& values) { return std::set<std::string>(values.begin(), values.end()); };
  auto const production_commands = as_set(ava::app::rpc::rpc_command_types());
  auto const production_events = as_set(ava::app::rpc::rpc_event_names());
  auto const production_errors = as_set(ava::app::rpc::rpc_stable_error_codes());
  expect(production_commands == as_set(manifest_commands) && production_events == as_set(manifest_events) && production_errors == as_set(manifest_errors) &&
             production_commands.size() == ava::app::rpc::rpc_command_types().size() && production_events.size() == ava::app::rpc::rpc_event_names().size() &&
             production_errors.size() == ava::app::rpc::rpc_stable_error_codes().size(),
         "RPC production command/event/error catalogs match the manifest bidirectionally without duplicate production entries");
  expect(ava::core::json::integer_field(manifest, "protocol_version") == ava::app::rpc::kRpcProtocolVersions.protocol &&
             ava::core::json::integer_field(manifest, "event_schema_version") == ava::app::rpc::kRpcProtocolVersions.event_schema,
         "RPC production protocol and event versions match the manifest");

  bool all_catalog_commands_parse = true;
  for (auto const type : ava::app::rpc::rpc_command_types())
  {
    auto parsed = ava::app::parse_rpc_command_line("{\"id\":\"catalog\",\"type\":\"" + std::string(type) + "\"}");
    all_catalog_commands_parse = all_catalog_commands_parse && parsed && parsed->type == type;
  }
  expect(all_catalog_commands_parse, "RPC parser recognizes every production-catalog command type before command-specific required-field dispatch");

  std::string actual_errors;
  actual_errors += ava::app::serialize_rpc_error_jsonl("bad", ava::app::rpc::invalid_rpc("RPC message must be a string"));
  actual_errors += ava::app::serialize_rpc_error_jsonl("busy", ava::app::rpc::active_run_reject_error("prompt"));
  actual_errors += ava::app::serialize_rpc_error_jsonl("cancelled", ava::app::rpc::canceled_error());
  actual_errors += ava::app::serialize_rpc_error_jsonl("follow", ava::app::rpc::skipped_follow_up_error("canceled"));
  expect(actual_errors == read_rpc_golden(golden / "errors.jsonl"),
         "RPC normative error golden is exact production serialization including category/code/details");

  std::string actual_envelopes;
  actual_envelopes += ava::app::serialize_rpc_success_jsonl("req-1", "{\"protocol_version\":1}");
  actual_envelopes += ava::app::serialize_rpc_error_jsonl("bad-1", ava::app::rpc::invalid_rpc("malformed RPC JSON object"));
  ava::app::EventEnvelope event;
  event.schema_version = 1;
  event.event_id = "event-1";
  event.timestamp = "2026-07-12T00:00:00Z";
  event.session_id = "session-1";
  event.request_id = "prompt-1";
  event.correlation_id = "prompt-1";
  event.name = "message_update";
  event.payload_type = "message";
  event.payload_json = "{\"text\":\"hello\",\"status\":\"streaming\"}";
  actual_envelopes += ava::app::serialize_event_envelope_jsonl(event);
  expect(actual_envelopes == read_rpc_golden(golden / "envelopes.jsonl"), "RPC response/event envelope golden is exact deterministic production serialization");

  auto const wire = read_rpc_golden(golden / "wire.jsonl");
  auto const first_newline = wire.find('\n');
  auto parsed_wire = ava::app::parse_rpc_command_line(wire.substr(0, first_newline));
  auto const actual_wire_response = ava::app::serialize_rpc_success_jsonl("protocol", ava::app::rpc::rpc_protocol_result_json());
  expect(parsed_wire && parsed_wire->type == "get_protocol" && wire.substr(first_newline + 1) == actual_wire_response,
         "RPC get_protocol golden request uses the production parser and its response uses the production serializer");
}

void test_app_rpc_contract_validation_regressions()
{
  std::vector<std::string> const malformed_recognized_fields = {
      R"JSON({"id":"scope","type":"permission_rule_add","action":"allow","operation":"read","scope":7,"reason":"x"})JSON",
      R"JSON({"id":"mode","type":"permission_rule_add","action":"allow","operation":"read","mode":false,"reason":"x"})JSON",
      R"JSON({"id":"provider","type":"set_model","provider":{},"model":"gpt-5.5"})JSON",
      R"JSON({"id":"display","type":"set_reasoning","reasoning_level":"low","reasoning_display":[]})JSON",
      R"JSON({"id":"compact","type":"compact","instructions":9})JSON",
      R"JSON({"id":"invoke","type":"invoke_command","name":"help","command_arguments":{}})JSON",
      R"JSON({"id":"question-answer","type":"question_reply","request_id":"q","correlation_id":"p","answer":1})JSON",
      R"JSON({"id":"question-selected","type":"question_reply","request_id":"q","correlation_id":"p","selected":true})JSON",
  };
  for (auto const& input : malformed_recognized_fields)
  {
    auto parsed = ava::app::parse_rpc_command_line(input);
    expect(!parsed && parsed.error().message().find("must be a string") != std::string::npos,
           "RPC parser rejects wrong-typed fields recognized by the selected command");
  }

  auto unrelated =
      ava::app::parse_rpc_command_line(R"JSON({"id":"state","type":"get_state","scope":7,"provider":{},"reasoning_display":[],"command_arguments":{}})JSON");
  expect(unrelated.has_value(), "RPC parser ignores malformed fields that are unrelated to the selected command");

  std::string invalid_utf8 = R"JSON({"id":"bad-utf8","type":"prompt","message":")JSON";
  invalid_utf8.push_back(static_cast<char>(0xC3));
  invalid_utf8 += R"JSON("})JSON";
  auto invalid = ava::app::parse_rpc_command_line(invalid_utf8);
  expect(!invalid && invalid.error().message() == "RPC request is not valid UTF-8", "RPC parser validates UTF-8 before JSON extraction");

  auto const invalid_response = ava::app::serialize_rpc_error_jsonl("", invalid.error());
  expect(invalid_response.find("\"code\":\"invalid_request\"") != std::string::npos &&
             std::ranges::all_of(invalid_response, [](char ch) { return static_cast<unsigned char>(ch) < 0x80U; }),
         "invalid UTF-8 produces an ASCII-safe recoverable error with stable code");

  ava::app::runtime::Event invalid_event;
  invalid_event.type = ava::app::runtime::EventType::ToolStart;
  invalid_event.tool_arguments_json = "{\"path\":\"";
  invalid_event.tool_arguments_json.push_back(static_cast<char>(0xFF));
  invalid_event.tool_arguments_json += "\"}";
  auto const serialized_invalid_event = ava::app::serialize_event_json(invalid_event);
  expect(serialized_invalid_event.find("\"args\":{") == std::string::npos && serialized_invalid_event.find("\"args_json\":") != std::string::npos &&
             ava::core::json::is_valid_utf8(serialized_invalid_event) && ava::core::json::is_valid_object(serialized_invalid_event),
         "RPC event serialization rejects invalid UTF-8 argument JSON from object-form event payloads");

  auto active = ava::app::rpc::active_run_reject_error("prompt");
  auto canceled = ava::app::rpc::canceled_error();
  auto skipped = ava::app::rpc::skipped_follow_up_error("canceled");
  expect(ava::app::serialize_rpc_error_jsonl("a", active).find("\"code\":\"active_run\"") != std::string::npos &&
             ava::app::serialize_rpc_error_jsonl("c", canceled).find("\"code\":\"canceled\"") != std::string::npos &&
             ava::app::serialize_rpc_error_jsonl("f", skipped).find("\"code\":\"follow_up_skipped\"") != std::string::npos,
         "RPC stable error codes cover active, canceled, and skipped follow-up branches");

  auto runtime_canceled = ava::core::Error(ava::core::ErrorCategory::Unknown, "agent loop canceled");
  runtime_canceled.with_context("boundary", "after_tool_dispatch");
  auto const runtime_canceled_json = ava::app::serialize_rpc_error_jsonl("runtime-canceled", runtime_canceled);
  auto const independent_unknown =
      ava::app::serialize_rpc_error_jsonl("provider-unknown", ava::core::Error(ava::core::ErrorCategory::Unknown, "provider returned an unknown failure"));
  expect(runtime_canceled_json.find("\"category\":\"unknown\",\"code\":\"canceled\",\"message\":\"agent loop canceled\"") != std::string::npos &&
             runtime_canceled_json.find("\"details\":\"unknown: agent loop canceled\\n  boundary: after_tool_dispatch\"") != std::string::npos &&
             independent_unknown.find("\"code\":\"internal_error\"") != std::string::npos,
         "RPC boundary classifies known runtime cancellation semantics without changing details or unrelated unknown errors");
}

void test_app_rpc_utf8_recovery_and_framing()
{
  std::istringstream framed(
      "{\"id\":\"one\",\"type\":\"get_state\"}\r\n"
      "{\"id\":\"two\",\"type\":\"get_state\"}");
  std::string line;
  auto first = ava::app::rpc::read_rpc_line_bounded(framed, line);
  expect(first && *first && line.ends_with('\r'), "RPC framing uses LF and retains one CR for the parser to strip");
  auto first_command = ava::app::parse_rpc_command_line(line);
  auto second = ava::app::rpc::read_rpc_line_bounded(framed, line);
  auto second_command = second && *second ? ava::app::parse_rpc_command_line(line)
                                          : ava::core::Result<ava::app::RpcCommand>{std::unexpected(ava::app::rpc::invalid_rpc("missing"))};
  auto eof = ava::app::rpc::read_rpc_line_bounded(framed, line);
  expect(first_command && first_command->id == "one" && second_command && second_command->id == "two" && eof && !*eof,
         "RPC framing accepts CRLF and processes an unterminated final JSON record before clean EOF");

  auto const root = create_empty_root("app-rpc-invalid-utf8-recovery");

  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  ava::app::runtime::OpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC invalid UTF-8 recovery test opens runtime session");
  if (!session)
    return;

  std::string const replacement = "\xEF\xBF\xBD";
  std::string invalid_component = "bad";
  invalid_component.push_back(static_cast<char>(0xFF));
  session->invocation_inputs().workspace_dir = std::filesystem::path(invalid_component);
  session->invocation_inputs().current_dir = std::filesystem::path(invalid_component);
  auto const invalid_state = ava::app::rpc::state_result_json(*session, false);
  auto const invalid_path = ava::app::rpc::string_field_json("path", invalid_component);
  ava::app::CommandResult invalid_output_result;
  invalid_output_result.handled = true;
  invalid_output_result.output = {invalid_component};
  auto const invalid_output = ava::app::rpc::command_result_json(invalid_output_result);
  expect(invalid_state.find("\"workspace_dir\":\"bad" + replacement + "\"") != std::string::npos &&
             invalid_state.find("\"current_dir\":\"bad" + replacement + "\"") != std::string::npos && invalid_path == "\"path\":\"bad" + replacement + "\"" &&
             invalid_output.find("\"output\":[\"bad" + replacement + "\"]") != std::string::npos &&
             invalid_output.find("\"text\":\"bad" + replacement + "\"") != std::string::npos,
         "RPC string serializers replace exact invalid workspace, path, output-array, and output-text bytes with U+FFFD");

  std::ostringstream boundary_stream;
  ava::app::rpc::output_ts boundary_output(boundary_stream, [] { });
  std::string raw_invalid_record = "{\"raw\":\"";
  raw_invalid_record.push_back(static_cast<char>(0xFF));
  raw_invalid_record += "\"}\n";
  auto boundary_written = ava::app::rpc::Output::write_record(boundary_output, raw_invalid_record);
  expect(boundary_written && boundary_stream.str() == "{\"raw\":\"" + replacement + "\"}\n" && ava::core::json::is_valid_utf8(boundary_stream.str()),
         "RPC sole output boundary validates and repairs the complete record before writing");

  std::string input = R"JSON({"id":"bad","type":"prompt","message":")JSON";
  input.push_back(static_cast<char>(0xFF));
  input += "\"}\n{\"id\":\"state-after\",\"type\":\"get_state\"}\n";
  std::istringstream in(input);
  std::ostringstream out;
  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({});
  ava::app::runtime::session_ts unlocked_session(std::move(*session));
  auto result = ava::app::run_rpc_loop(unlocked_session, open_options, provider, transport, {}, in, out, ava::app::rpc::RpcInputWake{});
  auto const jsonl = out.str();
  expect(result && jsonl.find("\"id\":\"\",\"type\":\"response\",\"success\":false") != std::string::npos &&
             jsonl.find("RPC request is not valid UTF-8") != std::string::npos && jsonl.find("\"id\":\"state-after\"") != std::string::npos &&
             ava::core::json::is_valid_utf8(jsonl),
         "RPC loop recovers after invalid UTF-8 with an empty-id valid UTF-8 JSON response");
}

void test_app_rpc_terminal_publication_gates_prompt_id_reuse()
{
  auto const root = create_empty_root("app-rpc-prompt-publication-gate");

  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  ava::app::runtime::OpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC prompt publication gate test opens runtime session");
  if (!session)
    return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({sse_response(final_text_sse("first terminal")), sse_response(final_text_sse("reused terminal"))});
  ava::app::runtime::RunOptions runtime_options;
  runtime_options.access_token = "token";
  BlockingInputBuf input_buffer;
  std::istream in(&input_buffer);
  TerminalPublicationStreamBuf output_buffer("\"id\":\"same\",\"type\":\"response\"");
  std::ostream out(&output_buffer);
  ava::core::VoidResult result;
  ava::app::runtime::session_ts unlocked_session(std::move(*session));
  std::jthread rpc_thread([&] {
    result = ava::app::run_rpc_loop(unlocked_session, open_options, provider, transport, runtime_options, in, out, [&] noexcept { input_buffer.close(); });
  });

  input_buffer.push("{\"id\":\"same\",\"type\":\"prompt\",\"message\":\"first\"}\n");
  bool const terminal_observable = output_buffer.wait_until_terminal(std::chrono::seconds(2));
  input_buffer.push("{\"id\":\"same\",\"type\":\"prompt\",\"message\":\"reuse after response\"}\n");
  output_buffer.release_terminal();
  bool const reused = output_buffer.wait_contains("reused terminal", std::chrono::seconds(2));
  input_buffer.close();
  rpc_thread.join();

  auto const jsonl = output_buffer.str();
  expect(result && terminal_observable && reused && transport.requests().size() == 2,
         "RPC prompt response publication gates admission and permits immediate sequential id reuse");
  expect(jsonl.find("RPC request id is already outstanding") == std::string::npos && count_substrings(jsonl, "\"id\":\"same\",\"type\":\"response\"") == 2,
         "RPC sequential prompt id reuse is not misclassified as a pipelined duplicate");
}

void test_app_rpc_parent_terminal_precedes_queued_follow_up_start()
{
  auto const root = create_empty_root("app-rpc-parent-follow-up-publication");

  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  auto const outside_path = root / "outside.txt";
  std::filesystem::create_directories(workspace);
  write_app_test_file(outside_path, "parent follow-up publication");
  ava::app::runtime::OpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC parent/follow-up publication test opens runtime session");
  if (!session)
    return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({sse_response(read_file_call_sse(outside_path.generic_string())), sse_response(final_text_sse("parent terminal")),
                                       sse_response(final_text_sse("child terminal"))});
  ava::app::runtime::RunOptions runtime_options;
  runtime_options.access_token = "token";
  BlockingInputBuf input_buffer;
  std::istream in(&input_buffer);
  TerminalPublicationStreamBuf output_buffer("\"id\":\"parent\",\"type\":\"response\"");
  std::ostream out(&output_buffer);
  ava::core::VoidResult result;
  ava::app::runtime::session_ts unlocked_session(std::move(*session));
  std::jthread rpc_thread([&] {
    result = ava::app::run_rpc_loop(unlocked_session, open_options, provider, transport, runtime_options, in, out, [&] noexcept { input_buffer.close(); });
  });

  input_buffer.push("{\"id\":\"parent\",\"type\":\"prompt\",\"message\":\"parent\"}\n");
  bool const permission_requested = output_buffer.wait_contains("\"name\":\"permission_requested\"", std::chrono::seconds(2));
  auto const resolver_id = rpc_string_field_from_output(output_buffer.str(), "resolver_request_id");
  input_buffer.push("{\"id\":\"child\",\"type\":\"follow_up\",\"message\":\"child\"}\n");
  input_buffer.push("{\"id\":\"allow\",\"type\":\"permission_reply\",\"request_id\":\"" + resolver_id.value_or("") +
                    "\",\"correlation_id\":\"parent\",\"decision\":\"allow\"}\n");
  bool const parent_observable = output_buffer.wait_until_terminal(std::chrono::seconds(2));
  bool const child_not_started_during_parent_flush = output_buffer.str().find("\"name\":\"follow_up_started\"") == std::string::npos;
  output_buffer.release_terminal();
  bool const child_completed = output_buffer.wait_contains("child terminal", std::chrono::seconds(2));
  input_buffer.close();
  rpc_thread.join();

  auto const jsonl = output_buffer.str();
  auto const parent_response = jsonl.find("\"id\":\"parent\",\"type\":\"response\"");
  auto const child_started = jsonl.find("\"name\":\"follow_up_started\"");
  auto const child_response = jsonl.find("\"id\":\"child\",\"type\":\"response\"");
  expect(result && permission_requested && resolver_id && parent_observable && child_not_started_during_parent_flush && child_completed,
         "RPC queued child remains unpublished while the parent terminal response flush is blocked");
  expect(parent_response < child_started && child_started < child_response,
         "RPC parent response remains ordered before follow_up_started and the queued child response");
}

void test_app_rpc_eof_during_blocked_parent_publication_skips_follow_up()
{
  auto const root = create_empty_root("app-rpc-eof-during-parent-publication");

  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  ava::app::runtime::OpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC blocked-publication EOF test opens runtime session");
  if (!session)
    return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  BlockingResponseTransport transport(sse_response(final_text_sse("parent terminal")));
  ava::app::runtime::RunOptions runtime_options;
  runtime_options.access_token = "token";
  BlockingInputBuf input_buffer;
  std::istream in(&input_buffer);
  PausingTerminalCallbackLineReader input(in, [&] noexcept { input_buffer.close(); });
  TerminalPublicationStreamBuf output_buffer("\"id\":\"parent\",\"type\":\"response\"");
  std::ostream out(&output_buffer);
  ava::core::VoidResult result;
  ava::app::runtime::session_ts unlocked_session(std::move(*session));
  std::jthread rpc_thread(
      [&] { result = ava::app::run_rpc_loop(unlocked_session, open_options, provider, transport, transport, runtime_options, input, out); });

  input_buffer.push("{\"id\":\"parent\",\"type\":\"prompt\",\"message\":\"parent\"}\n");
  bool const parent_requested = transport.wait_for_request(std::chrono::seconds(2));
  input_buffer.push("{\"id\":\"child\",\"type\":\"follow_up\",\"message\":\"must not run\"}\n");
  bool const child_queued = output_buffer.wait_contains("\"name\":\"follow_up_queued\"", std::chrono::seconds(2));
  transport.release();
  bool const parent_publication_blocked = output_buffer.wait_until_terminal(std::chrono::seconds(2));
  input_buffer.close();
  bool const terminal_state_published = input.wait_until_terminal_callback(std::chrono::seconds(2));
  output_buffer.release_terminal();
  bool const child_skipped_while_reader_paused = output_buffer.wait_contains("\"name\":\"follow_up_skipped\"", std::chrono::seconds(2));
  input.release_terminal_callback();
  rpc_thread.join();

  auto const jsonl = output_buffer.str();
  expect(result && parent_requested && child_queued && parent_publication_blocked && terminal_state_published && child_skipped_while_reader_paused,
         "RPC EOF terminal state is published before the parent response flush is released while the reader remains paused");
  expect(transport.requests().size() == 1 && jsonl.find("\"name\":\"follow_up_started\"") == std::string::npos &&
             jsonl.find("\"id\":\"child\",\"type\":\"response\",\"success\":true") == std::string::npos,
         "RPC EOF during parent publication cannot start or run the queued child");
  auto const parent_response = jsonl.find("\"id\":\"parent\",\"type\":\"response\"");
  auto const child_skipped = jsonl.find("\"name\":\"follow_up_skipped\"");
  auto const child_error = jsonl.find("\"id\":\"child\",\"type\":\"response\",\"success\":false");
  expect(jsonl.find("\"request_id\":\"child\"") != std::string::npos && child_skipped != std::string::npos &&
             jsonl.find("\"reason\":\"canceled\"") != std::string::npos && child_error != std::string::npos && parent_response < child_skipped &&
             child_skipped < child_error,
         "RPC EOF publishes the parent response before the queued child skipped event and canceled response once output becomes writable");
}

void test_app_rpc_terminal_publication_gates_direct_and_compaction_runs()
{
  {
    auto const root = create_empty_root("app-rpc-direct-publication-gate");

    auto const workspace = root / "workspace";
    auto const paths = app_test_paths(root);
    std::filesystem::create_directories(workspace);
    ava::app::runtime::OpenOptions open_options;
    open_options.workspace_dir = workspace;
    open_options.current_dir = workspace;
    open_options.paths = paths;
    auto session = ava::app::open_runtime_session(open_options);
    expect(session.has_value(), "RPC direct publication gate test opens runtime session");
    if (session)
    {
      ava::provider::OpenAIProvider const provider("https://api.example.test");
      ava::tests::FakeTransport transport({sse_response(final_text_sse("prompt after direct"))});
      ava::app::runtime::RunOptions runtime_options;
      runtime_options.access_token = "token";
      runtime_options.permission_resolver = [](ava::permissions::PermissionPrompt const&) -> ava::core::Result<ava::permissions::PermissionResolutionDecision> {
        return ava::permissions::PermissionResolutionDecision{ava::permissions::PermissionResolution::Allow, "publication gate test"};
      };
      BlockingInputBuf input_buffer;
      std::istream in(&input_buffer);
      TerminalPublicationStreamBuf output_buffer("\"id\":\"direct\",\"type\":\"response\"");
      std::ostream out(&output_buffer);
      ava::core::VoidResult result;
      ava::app::runtime::session_ts unlocked_session(std::move(*session));
      std::jthread rpc_thread([&] {
        result = ava::app::run_rpc_loop(unlocked_session, open_options, provider, transport, runtime_options, in, out, [&] noexcept { input_buffer.close(); });
      });
      input_buffer.push("{\"id\":\"direct\",\"type\":\"run_bash\",\"command\":\"printf direct\"}\n");
      bool const terminal_observable = output_buffer.wait_until_terminal(std::chrono::seconds(2));
      input_buffer.push("{\"id\":\"next\",\"type\":\"prompt\",\"message\":\"after direct\"}\n");
      output_buffer.release_terminal();
      bool const next_completed = output_buffer.wait_contains("prompt after direct", std::chrono::seconds(2));
      input_buffer.close();
      rpc_thread.join();
      auto const jsonl = output_buffer.str();
      expect(result && terminal_observable && next_completed && jsonl.find("\"code\":\"active_run\"") == std::string::npos,
             "RPC direct terminal publication settles active state before admitting an immediately submitted prompt");
    }
  }

  {
    auto const root = create_empty_root("app-rpc-compaction-publication-gate");

    auto const workspace = root / "workspace";
    auto const paths = app_test_paths(root);
    std::filesystem::create_directories(workspace);
    ava::app::runtime::OpenOptions open_options;
    open_options.workspace_dir = workspace;
    open_options.current_dir = workspace;
    open_options.paths = paths;
    auto session = ava::app::open_runtime_session(open_options);
    expect(session.has_value(), "RPC compaction publication gate test opens runtime session");
    if (session)
    {
      ava::provider::OpenAIProvider const provider("https://api.example.test");
      ava::tests::FakeTransport transport({ava::provider::HttpResponse{.status_code = 500, .headers = {}, .body = "{\"error\":{\"message\":\"first\"}}"},
                                           ava::provider::HttpResponse{.status_code = 500, .headers = {}, .body = "{\"error\":{\"message\":\"second\"}}"}});
      ava::app::runtime::RunOptions runtime_options;
      runtime_options.access_token = "token";
      BlockingInputBuf input_buffer;
      std::istream in(&input_buffer);
      TerminalPublicationStreamBuf output_buffer("\"id\":\"compact\",\"type\":\"response\"");
      std::ostream out(&output_buffer);
      ava::core::VoidResult result;
      ava::app::runtime::session_ts unlocked_session(std::move(*session));
      std::jthread rpc_thread([&] {
        result = ava::app::run_rpc_loop(unlocked_session, open_options, provider, transport, runtime_options, in, out, [&] noexcept { input_buffer.close(); });
      });
      input_buffer.push("{\"id\":\"compact\",\"type\":\"compact\"}\n");
      bool const terminal_observable = output_buffer.wait_until_terminal(std::chrono::seconds(2));
      input_buffer.push("{\"id\":\"compact\",\"type\":\"compact\"}\n");
      output_buffer.release_terminal();
      bool const second_completed = output_buffer.wait_for_occurrences("\"id\":\"compact\",\"type\":\"response\"", 2, std::chrono::seconds(2));
      input_buffer.close();
      rpc_thread.join();
      auto const jsonl = output_buffer.str();
      expect(result && terminal_observable && second_completed, "RPC compaction terminal publication completes both observable sequential responses");
      expect(transport.requests().size() == 2 && jsonl.find("RPC request id is already outstanding") == std::string::npos,
             "RPC compaction terminal publication permits immediate sequential id reuse after observable response");
    }
  }
}

void test_app_rpc_worker_output_failure_wakes_blocked_input()
{
  auto const root = create_empty_root("app-rpc-output-failure-wake");

  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  ava::app::runtime::OpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC output-failure wake test opens runtime session");
  if (!session)
    return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  BlockingResponseTransport transport(sse_response(final_text_sse("terminal write fails")));
  ava::app::runtime::RunOptions runtime_options;
  runtime_options.access_token = "token";
  BlockingInputBuf input_buffer;
  std::istream in(&input_buffer);
  TerminalPublicationStreamBuf output_buffer("\"id\":\"p1\",\"type\":\"response\"", true);
  std::ostream out(&output_buffer);
  ava::core::VoidResult result;
  ava::app::runtime::session_ts unlocked_session(std::move(*session));
  std::jthread rpc_thread([&] {
    result = ava::app::run_rpc_loop(unlocked_session, open_options, provider, transport, transport, runtime_options, in, out,
                                    [&] noexcept { input_buffer.close(); });
  });
  input_buffer.push("{\"id\":\"p1\",\"type\":\"prompt\",\"message\":\"fail output\"}\n");
  bool const request_started = transport.wait_for_request(std::chrono::seconds(2));
  bool const input_blocked = input_buffer.wait_until_blocked(std::chrono::seconds(2));
  transport.release();
  bool const exited_without_more_input = input_buffer.wait_until_eof_observed(std::chrono::seconds(2));
  rpc_thread.join();

  expect(request_started && input_blocked && exited_without_more_input,
         "public std::istream RPC loop wakes a blocked input reader after worker output failure without another stdin record");
  expect(!result && result.error().category() == ava::core::ErrorCategory::Io && result.error().message() == "failed to write RPC JSONL record",
         "RPC output-failure wake returns the original worker output I/O error after joining");
}

void test_app_rpc_mode_forwards_nonstdin_wake()
{
  auto const root = create_empty_root("app-rpc-mode-stream-wake");

  auto const workspace = root / "workspace";
  std::filesystem::create_directories(workspace);
  ava::app::RpcModeOptions options;
  options.open_options.workspace_dir = workspace;
  options.open_options.current_dir = workspace;
  options.open_options.paths = app_test_paths(root);
  options.open_options.offline = true;
  BlockingInputBuf input_buffer;
  std::istream in(&input_buffer);
  std::ostringstream out;
  out.setstate(std::ios::badbit);
  std::ostringstream err;
  bool wake_called = false;
  input_buffer.push("{\"id\":\"protocol\",\"type\":\"get_protocol\"}\n");
  auto const status = ava::app::run_rpc_mode(options, in, out, err, [&] noexcept {
    wake_called = true;
    input_buffer.close();
  });

  expect(status == 1 && wake_called && err.str().find("failed to write RPC JSONL record") != std::string::npos,
         "run_rpc_mode forwards an explicit wake callback for a non-std::cin stream on output failure");
}

void test_app_rpc_posix_line_reader_wake_eof_and_fd_lifetime()
{
  using ava::app::rpc::RpcInputTerminalOutcome;

  auto fd_count = [] {
    std::error_code error;
    std::size_t count = 0;
    for (auto const& entry : std::filesystem::directory_iterator("/proc/self/fd", error))
    {
      static_cast<void>(entry);
      ++count;
    }
    return error ? std::optional<std::size_t>{} : std::optional<std::size_t>{count};
  };
  auto const before = fd_count();
  bool framing_ok = true;
  bool races_ok = true;
  for (int iteration = 0; iteration < 32; ++iteration)
  {
    int fds[2] = {-1, -1};
    if (pipe(fds) != 0)
    {
      races_ok = false;
      break;
    }
    auto reader = ava::app::rpc::make_posix_rpc_line_reader(fds[0]);
    if (!reader)
    {
      close(fds[0]);
      close(fds[1]);
      races_ok = false;
      break;
    }
    if (iteration == 0)
    {
      std::string const records = "one\r\ntwo";
      auto const bytes_written = write(fds[1], records.data(), records.size());
      close(fds[1]);
      std::vector<RpcInputTerminalOutcome> outcomes;
      std::string line;
      auto first = (*reader)->read_line(line, [&outcomes](RpcInputTerminalOutcome outcome) { outcomes.push_back(outcome); });
      framing_ok = bytes_written == static_cast<ssize_t>(records.size()) && first && *first && line == "one\r";
      auto second = (*reader)->read_line(line, [&outcomes](RpcInputTerminalOutcome outcome) { outcomes.push_back(outcome); });
      framing_ok = framing_ok && second && *second && line == "two";
      auto eof = (*reader)->read_line(line, [&outcomes](RpcInputTerminalOutcome outcome) { outcomes.push_back(outcome); });
      framing_ok = framing_ok && eof && !*eof &&
                   outcomes == std::vector<RpcInputTerminalOutcome>{RpcInputTerminalOutcome::EofWithFinalRecord, RpcInputTerminalOutcome::Eof};
    }
    else
    {
      ava::core::Result<bool> read_result = true;
      std::vector<RpcInputTerminalOutcome> outcomes;
      std::string line;
      std::jthread reading([&] { read_result = (*reader)->read_line(line, [&outcomes](RpcInputTerminalOutcome outcome) { outcomes.push_back(outcome); }); });
      (*reader)->cancel();
      close(fds[1]);
      reading.join();
      races_ok = races_ok && read_result && !*read_result && outcomes == std::vector<RpcInputTerminalOutcome>{RpcInputTerminalOutcome::Canceled};
    }
    close(fds[0]);
  }

  int error_fds[2] = {-1, -1};
  bool error_outcome_ok = false;
  if (pipe(error_fds) == 0)
  {
    auto reader = ava::app::rpc::make_posix_rpc_line_reader(error_fds[0]);
    if (reader)
    {
      close(error_fds[0]);
      std::vector<RpcInputTerminalOutcome> outcomes;
      std::string line;
      auto error = (*reader)->read_line(line, [&outcomes](RpcInputTerminalOutcome outcome) { outcomes.push_back(outcome); });
      error_outcome_ok = !error && error.error().category() == ava::core::ErrorCategory::Io &&
                         outcomes == std::vector<RpcInputTerminalOutcome>{RpcInputTerminalOutcome::Error};
    }
    else
    {
      close(error_fds[0]);
    }
    close(error_fds[1]);
  }
  auto const after = fd_count();
  expect(framing_ok && races_ok && error_outcome_ok,
         "RPC POSIX line reader preserves framing and synchronously classifies EOF, final records, wake cancellation, and errors");
  expect(!before || !after || *before == *after, "RPC POSIX line reader wake pipes do not leak file descriptors");
}

void test_app_rpc_stream_line_reader_terminal_outcomes()
{
  using ava::app::rpc::RpcInputTerminalOutcome;

  std::vector<RpcInputTerminalOutcome> outcomes;
  std::string line;
  std::istringstream empty_input;
  ava::app::rpc::StreamRpcLineReader empty_reader(empty_input, ava::app::rpc::RpcInputWake{});
  auto empty_eof = empty_reader.read_line(line, [&outcomes](RpcInputTerminalOutcome outcome) { outcomes.push_back(outcome); });

  std::istringstream final_input("final record");
  ava::app::rpc::StreamRpcLineReader final_reader(final_input, ava::app::rpc::RpcInputWake{});
  auto final_record = final_reader.read_line(line, [&outcomes](RpcInputTerminalOutcome outcome) { outcomes.push_back(outcome); });
  bool const final_record_read = final_record && *final_record && line == "final record";
  auto final_eof = final_reader.read_line(line, [&outcomes](RpcInputTerminalOutcome outcome) { outcomes.push_back(outcome); });

  std::istringstream newline_input("complete\n");
  ava::app::rpc::StreamRpcLineReader newline_reader(newline_input, ava::app::rpc::RpcInputWake{});
  auto complete_record = newline_reader.read_line(line, [&outcomes](RpcInputTerminalOutcome outcome) { outcomes.push_back(outcome); });
  bool const normal_record_silent = complete_record && *complete_record && line == "complete" && outcomes.size() == 3;

  BlockingInputBuf canceled_input_buffer;
  canceled_input_buffer.push("partial final record");
  std::istream canceled_input(&canceled_input_buffer);
  ava::app::rpc::StreamRpcLineReader canceled_reader(canceled_input, [&] noexcept { canceled_input_buffer.close(); });
  ava::core::Result<bool> canceled = true;
  std::jthread canceled_read(
      [&] { canceled = canceled_reader.read_line(line, [&outcomes](RpcInputTerminalOutcome outcome) { outcomes.push_back(outcome); }); });
  bool const canceled_while_partial = canceled_input_buffer.wait_until_blocked(std::chrono::seconds(2));
  canceled_reader.cancel();
  canceled_read.join();

  std::istringstream error_input("unread");
  error_input.setstate(std::ios::badbit);
  ava::app::rpc::StreamRpcLineReader error_reader(error_input, ava::app::rpc::RpcInputWake{});
  auto read_error = error_reader.read_line(line, [&outcomes](RpcInputTerminalOutcome outcome) { outcomes.push_back(outcome); });

  expect(empty_eof && !*empty_eof && final_record_read && final_eof && !*final_eof && normal_record_silent && canceled_while_partial && canceled &&
             !*canceled && !read_error && read_error.error().category() == ava::core::ErrorCategory::Io &&
             outcomes == std::vector<RpcInputTerminalOutcome>{RpcInputTerminalOutcome::Eof, RpcInputTerminalOutcome::EofWithFinalRecord,
                                                              RpcInputTerminalOutcome::Eof, RpcInputTerminalOutcome::Canceled, RpcInputTerminalOutcome::Error},
         "RPC stream line reader synchronously classifies empty EOF, final records, cancellation, and input errors");
}

void test_app_rpc_stream_reader_wake_badbit_is_canceled()
{
  using ava::app::rpc::RpcInputTerminalOutcome;

  WakeBadInputBuf input_buffer;
  std::istream in(&input_buffer);
  ava::app::rpc::StreamRpcLineReader reader(in, [&] noexcept { input_buffer.wake(); });
  std::vector<RpcInputTerminalOutcome> outcomes;
  std::string line;
  ava::core::Result<bool> result = true;
  std::jthread reader_thread([&] { result = reader.read_line(line, [&outcomes](RpcInputTerminalOutcome outcome) { outcomes.push_back(outcome); }); });
  bool const blocked = input_buffer.wait_until_blocked(std::chrono::seconds(2));
  reader.cancel();
  reader_thread.join();

  expect(blocked && in.bad() && result && !*result && outcomes == std::vector<RpcInputTerminalOutcome>{RpcInputTerminalOutcome::Canceled},
         "RPC stream reader classifies a wake-induced bad stream as canceled rather than an input I/O error");
}

void test_app_rpc_unterminated_final_command_executes()
{
  auto const root = create_empty_root("app-rpc-unterminated-final-command");

  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  ava::app::runtime::OpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC unterminated final command test opens runtime session");
  if (!session)
    return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({});
  ava::app::runtime::RunOptions runtime_options;
  std::istringstream in("{\"id\":\"protocol\",\"type\":\"get_protocol\"}");
  std::ostringstream out;
  ava::app::runtime::session_ts unlocked_session(std::move(*session));
  auto result = ava::app::run_rpc_loop(unlocked_session, open_options, provider, transport, runtime_options, in, out, ava::app::rpc::RpcInputWake{});
  auto const jsonl = out.str();

  expect(result && jsonl.find("\"id\":\"protocol\",\"type\":\"response\",\"success\":true") != std::string::npos,
         "RPC loop executes an unterminated final command before observing EOF closure");
}

void test_app_rpc_newline_terminated_oversized_line_recovers()
{
  auto const root = create_empty_root("app-rpc-oversized-line-recovery");

  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  ava::app::runtime::OpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC oversized-line recovery test opens runtime session");
  if (!session)
    return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({});
  ava::app::runtime::RunOptions runtime_options;
  std::string input(ava::app::rpc::kMaxRpcLineBytes + 1, 'x');
  input += "\n{\"id\":\"protocol\",\"type\":\"get_protocol\"}\n";
  std::istringstream in(std::move(input));
  std::ostringstream out;
  ava::app::runtime::session_ts unlocked_session(std::move(*session));
  auto result = ava::app::run_rpc_loop(unlocked_session, open_options, provider, transport, runtime_options, in, out, ava::app::rpc::RpcInputWake{});
  auto const jsonl = out.str();

  expect(result && count_substrings(jsonl, "\"id\":\"\",\"type\":\"response\",\"success\":false") == 1 &&
             jsonl.find("RPC request line is too large") != std::string::npos &&
             jsonl.find("\"id\":\"protocol\",\"type\":\"response\",\"success\":true") != std::string::npos,
         "RPC newline-terminated oversized line is recoverable and the next valid record executes");
}

void test_app_rpc_compact_cancellation_is_error_response_without_provider_request()
{
  auto const root = create_empty_root("app-rpc-compact-canceled");

  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::runtime::OpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC compact cancellation test opens runtime session");
  if (!session)
    return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({ava::provider::HttpResponse{.status_code = 200, .headers = {}, .body = "{\"output_text\":\"unused\"}"}});
  std::istringstream in("{\"id\":\"cmp-cancel\",\"type\":\"compact\"}\n");
  std::ostringstream out;
  ava::app::runtime::RunOptions runtime_options;
  runtime_options.access_token = "token";
  runtime_options.cancel_requested = [] { return true; };

  ava::app::runtime::session_ts unlocked_session(std::move(*session));
  auto result = ava::app::run_rpc_loop(unlocked_session, open_options, provider, transport, runtime_options, in, out, ava::app::rpc::RpcInputWake{});
  ava::app::runtime::session_ts::rat session_r(unlocked_session);
  auto const jsonl = out.str();
  auto entries = session_r->store.load();
  expect(result.has_value(), "RPC compact cancellation loop completes after error response");
  expect(jsonl.find("\"id\":\"cmp-cancel\"") != std::string::npos && jsonl.find("\"success\":false") != std::string::npos &&
             jsonl.find("agent loop canceled") != std::string::npos,
         "RPC compact cancellation is returned as a machine-readable error response");
  expect(transport.requests().empty(), "RPC compact cancellation avoids dispatching a provider summary request");
  expect(entries && count_compaction_entries(*entries) == 0, "RPC compact cancellation leaves session without a compaction entry");
}

}  // namespace

void run_app_rpc_tests()
{
  test_app_rpc_prompt_payload_serialization();
  test_app_rpc_prompt_result_tool_timeline_golden_payloads();
  test_app_rpc_parsing_and_response_serialization();
  test_app_rpc_identifier_validation();
  test_app_rpc_prompt_with_fake_transport_streams_events();
  test_app_rpc_offline_allows_local_protocol_and_rejects_prompt_before_provider_request();
  test_app_rpc_prompt_imports_image_attachments();
  test_app_rpc_prompt_imports_inline_image_uploads();
  test_app_rpc_prompt_rejects_inline_image_upload_mime_mismatch();
  test_app_rpc_prompt_streams_provider_deltas_before_final_response();
  test_app_rpc_prompt_retry_transport_cancellation_is_canceled_event();
  test_app_rpc_prompt_after_idle_cancel_clears_cancel_flag();
  test_app_rpc_prompt_refreshes_expired_oauth_before_provider_request();
  test_app_rpc_malformed_line_recovery_and_unknown_command();
  test_app_rpc_state_list_sessions_and_open_session();
  test_app_rpc_job_controls_are_active_safe_and_redacted();
  test_app_rpc_current_session_reads_reject_path_replacement();
  test_app_rpc_session_metadata_name_and_labels();
  test_app_rpc_session_tree_command_and_switch_navigation();
  test_app_rpc_session_fork_and_clone_commands();
  test_app_rpc_branch_construction_failure_rolls_back_created_file();
  test_app_rpc_noncurrent_branch_source_recovers_torn_tail();
  test_app_rpc_summarize_branch_appends_to_source_session();
  test_app_rpc_model_commands();
  test_app_rpc_reasoning_commands();
  test_app_rpc_reasoning_model_serialization_exposes_resolved_maps();
  test_app_rpc_protocol_version_and_session_commands();
  test_app_rpc_messages_keep_v1_payloads_when_ordered_output_does_not_fit();
  test_app_rpc_protocol_version_and_resolver_reply_errors();
  test_app_rpc_mcp_command_responses();
  test_app_rpc_command_responses_for_context_compact_export();
  test_app_rpc_direct_run_command_permission_reply_executes_and_audits();
  test_app_rpc_direct_run_command_permission_denial_blocks_execution();
  test_app_rpc_direct_run_command_active_rejects_and_cancels_process();
  test_app_rpc_compact_provider_failure_is_error_response();
  test_app_rpc_production_catalog_and_golden_contract();
  test_app_rpc_contract_validation_regressions();
  test_app_rpc_utf8_recovery_and_framing();
  test_app_rpc_terminal_publication_gates_prompt_id_reuse();
  test_app_rpc_parent_terminal_precedes_queued_follow_up_start();
  test_app_rpc_eof_during_blocked_parent_publication_skips_follow_up();
  test_app_rpc_terminal_publication_gates_direct_and_compaction_runs();
  test_app_rpc_worker_output_failure_wakes_blocked_input();
  test_app_rpc_mode_forwards_nonstdin_wake();
  test_app_rpc_posix_line_reader_wake_eof_and_fd_lifetime();
  test_app_rpc_stream_line_reader_terminal_outcomes();
  test_app_rpc_stream_reader_wake_badbit_is_canceled();
  test_app_rpc_unterminated_final_command_executes();
  test_app_rpc_newline_terminated_oversized_line_recovers();
  test_app_rpc_compact_cancellation_is_error_response_without_provider_request();
}
