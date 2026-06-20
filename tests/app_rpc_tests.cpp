#include "ava/app/headless_policy.h"
#include "ava/app/rpc/serialization.h"
#include "ava/app/rpc/handlers.h"
#include "ava/app/rpc_mode.h"
#include "ava/app/runtime.h"

#include "ava/agent/question.h"

#include "ava/config/auth.h"

#include "ava/session/session_metadata.h"
#include "ava/session/session_store.h"
#include "ava/session/validation.h"

#include "ava/permissions/permission.h"

#include "ava/provider/openai_provider.h"

#include "ava/core/ids.h"
#include "ava/core/json.h"

#include "tests/support/app_runtime_support.h"
#include "tests/support/fake_transport.h"
#include "tests/support/test_harness.h"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <ranges>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#ifndef AVA_FAKE_MCP_SERVER_PATH
#define AVA_FAKE_MCP_SERVER_PATH ""
#endif

namespace {

using namespace ava::tests;

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
  prompt_result.stop_reason = "completed";
  prompt_result.tool_calls = 1;
  prompt_result.tool_timeline.push_back(tool_entry);
  auto const prompt_json = ava::app::rpc::prompt_result_json("session_1", prompt_result);
  expect(prompt_json.find("\"tool_timeline\"") != std::string::npos &&
             prompt_json.find("\"structured_result\":{\"schema_version\":1") != std::string::npos &&
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
  prompt_result.stop_reason = "completed";
  prompt_result.provider_iterations = 2;
  prompt_result.tool_calls = 3;
  prompt_result.tool_timeline = {success_entry, denied_entry, canceled_entry};

  auto const success_entry_json =
      std::string{"{\"status\":\"success\",\"call_id\":\"call_success\",\"tool\":\"write_file\","} +
      "\"text\":\"wrote note\",\"result_summary\":\"wrote note\",\"args\":{\"path\":\"note.txt\"}," +
      "\"result\":{\"ok\":true,\"path\":\"note.txt\"},\"structured_result\":" + success_structured +
      ",\"content_type\":\"application/json\",\"diff\":\"--- note.txt\\n+++ note.txt\\n-old\\n+new\"," +
      "\"changed_paths\":[\"note.txt\"],\"permission_request_ids\":[\"permreq_write\"]," +
      "\"spill_path\":\"spill/tool.txt\",\"diff_truncated\":true,\"truncated\":true,"
      "\"byte_limited\":true,\"line_limited\":true,\"spill_truncated\":true,\"output_bytes\":128,"
      "\"total_bytes\":512,\"output_lines\":2,\"total_lines\":5,\"start_line\":1,\"end_line\":2,"
      "\"next_offset_line\":3,\"omitted_bytes\":384,\"omitted_lines\":3}";
  auto const denied_entry_json =
      std::string{"{\"status\":\"error\",\"call_id\":\"call_denied\",\"tool\":\"write_file\","} +
      "\"text\":\"permission denied\",\"result_summary\":\"permission denied\",\"structured_result\":" +
      denied_structured +
      ",\"content_type\":\"text/plain\",\"category\":\"permission\",\"error_code\":\"permission_denied\"," +
      "\"message\":\"permission denied\",\"details\":\"resolution: deny\"}";
  auto const canceled_entry_json =
      std::string{"{\"status\":\"canceled\",\"call_id\":\"call_canceled\",\"tool\":\"glob\","} +
      "\"text\":\"tool canceled\",\"result_summary\":\"tool canceled\",\"structured_result\":" + canceled_structured +
      ",\"content_type\":\"text/plain\",\"category\":\"canceled\",\"error_code\":\"tool_canceled\"," +
      "\"message\":\"tool call canceled\",\"details\":\"cancel requested\"}";
  auto const expected_json =
      std::string{"{\"session_id\":\"session_rpc\",\"final_text\":\"done\",\"stop_reason\":\"completed\","} +
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
  expect(branch_summary && branch_summary->branch_root_entry_id && *branch_summary->branch_root_entry_id == "entry_1" &&
             branch_summary->branch_tip_entry_id && *branch_summary->branch_tip_entry_id == "entry_2" && branch_summary->summary &&
             *branch_summary->summary == "line one\nline two" && branch_summary->provider && *branch_summary->provider == "openai" &&
             branch_summary->model && *branch_summary->model == "gpt-test" && branch_summary->reason && *branch_summary->reason == "user requested",
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

  auto non_object_plugin_command_args = ava::app::parse_rpc_command_line(
      R"JSON({"id":"plugin","type":"run_plugin_command","plugin_id":"com.example.tool","name":"status","arguments":123})JSON");
  expect(!non_object_plugin_command_args && non_object_plugin_command_args.error().message() == "RPC arguments must be an object",
         "RPC parser rejects non-object plugin command arguments instead of silently defaulting");

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
}

void test_app_rpc_prompt_with_fake_transport_streams_events()
{
  auto const root = temp_root() / "app-rpc-prompt";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::RuntimeOpenOptions open_options;
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
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.access_token = "token";
  BlockingInputBuf input_buffer;
  std::istream in(&input_buffer);
  ThreadSafeStringBuf output_buffer;
  std::ostream out(&output_buffer);
  ava::core::VoidResult result;
  std::jthread rpc_thread([&] { result = ava::app::run_rpc_loop(*session, open_options, provider, transport, runtime_options, in, out); });
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

void test_app_rpc_prompt_streams_provider_deltas_before_final_response()
{
  auto const root = temp_root() / "app-rpc-prompt-streaming";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::RuntimeOpenOptions open_options;
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
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.access_token = "token";
  BlockingInputBuf input_buffer;
  std::istream in(&input_buffer);
  ThreadSafeStringBuf output_buffer;
  std::ostream out(&output_buffer);
  ava::core::VoidResult result;
  std::jthread rpc_thread([&] { result = ava::app::run_rpc_loop(*session, open_options, provider, transport, runtime_options, in, out); });
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
  auto const root = temp_root() / "app-rpc-prompt-retry-canceled";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.mode = ava::agent::Mode::Build;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC retry-cancel prompt test opens runtime session");
  if (!session)
    return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport(
      {ava::provider::HttpResponse{.status_code = 500, .headers = {}, .body = "{\"error\":{\"message\":\"retry later\"}}"},
       ava::provider::HttpResponse{.status_code = 500, .headers = {}, .body = "{\"error\":{\"message\":\"retry later\"}}"},
       ava::provider::HttpResponse{.status_code = 500, .headers = {}, .body = "{\"error\":{\"message\":\"retry later\"}}"}});
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.access_token = "token";
  runtime_options.enable_transport_retries = true;

  BlockingInputBuf input_buffer;
  std::istream in(&input_buffer);
  ThreadSafeStringBuf output_buffer;
  std::ostream out(&output_buffer);
  ava::core::VoidResult result;
  std::jthread rpc_thread([&] { result = ava::app::run_rpc_loop(*session, open_options, provider, transport, runtime_options, in, out); });
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
  expect(jsonl.find("\"name\":\"canceled\"") != std::string::npos &&
             jsonl.find("\"payload_type\":\"cancellation\"") != std::string::npos &&
             jsonl.find("\"name\":\"error\"") == std::string::npos,
         "RPC retry transport cancellation emits terminal canceled envelope instead of an error envelope");
}

void test_app_rpc_prompt_after_idle_cancel_clears_cancel_flag()
{
  auto const root = temp_root() / "app-rpc-prompt-after-idle-cancel";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::RuntimeOpenOptions open_options;
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
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.access_token = "token";
  BlockingInputBuf input_buffer;
  std::istream in(&input_buffer);
  ThreadSafeStringBuf output_buffer;
  std::ostream out(&output_buffer);
  ava::core::VoidResult result;
  std::jthread rpc_thread([&] { result = ava::app::run_rpc_loop(*session, open_options, provider, transport, runtime_options, in, out); });
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
  auto const root = temp_root() / "app-rpc-oauth-refresh";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
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

  ava::app::RuntimeOpenOptions open_options;
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
  std::jthread rpc_thread([&] { result = ava::app::run_rpc_loop(*session, open_options, provider, transport, ava::app::RuntimeRunOptions{}, in, out); });
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
  auto const root = temp_root() / "app-rpc-recovery";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::RuntimeOpenOptions open_options;
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
  auto result = ava::app::run_rpc_loop(*session, open_options, provider, transport, ava::app::RuntimeRunOptions{}, in, out);
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
  auto const root = temp_root() / "app-rpc-state";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::RuntimeOpenOptions open_options;
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

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({});
  std::istringstream in(
      "{\"id\":\"state\",\"type\":\"get_state\"}\n"
      "{\"id\":\"list\",\"type\":\"list_sessions\"}\n"
      "{\"id\":\"open\",\"type\":\"open_session\",\"session_id\":\"" +
      first_id + "\"}\n");
  std::ostringstream out;
  auto result = ava::app::run_rpc_loop(*second, open_options, provider, transport, ava::app::RuntimeRunOptions{}, in, out);
  auto const jsonl = out.str();
  expect(result.has_value(), "RPC state/list/open loop completes successfully");
  expect(jsonl.find("\"id\":\"state\"") != std::string::npos && jsonl.find(second_id) != std::string::npos &&
             jsonl.find("\"id\":\"list\"") != std::string::npos && jsonl.find(first_id) != std::string::npos &&
             jsonl.find("\"id\":\"open\"") != std::string::npos,
         "RPC state, list_sessions, and open_session return session metadata");
  expect(second->store.session_id() == first_id, "RPC open_session switches the active runtime session");
}

void test_app_rpc_session_metadata_name_and_labels()
{
  auto const root = temp_root() / "app-rpc-session-metadata";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::RuntimeOpenOptions open_options;
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
  auto result = ava::app::run_rpc_loop(*session, open_options, provider, transport, ava::app::RuntimeRunOptions{}, in, out);
  auto const jsonl = out.str();
  auto metadata = ava::session::load_session_metadata(session->store);
  auto entries = session->store.load();
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
  auto const root = temp_root() / "app-rpc-session-tree";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::RuntimeOpenOptions open_options;
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
  auto parent_meta = ava::session::append_session_metadata(parent->store, std::move(parent_metadata));

  ava::session::SessionMetadataUpdate child_metadata;
  child_metadata.name = "Child";
  child_metadata.labels = std::vector<std::string>{"branch"};
  child_metadata.archived = true;
  child_metadata.parent_session_id = parent_id;
  child_metadata.source_session_id = parent_id;
  child_metadata.branch_from_entry_id = parent_entries->front().id;
  child_metadata.branch_origin = "fork";
  child_metadata.actor = "test";
  auto child_meta = ava::session::append_session_metadata(child->store, std::move(child_metadata));
  expect(parent_meta && child_meta, "RPC session_tree test persists branch metadata");

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({});
  auto const requests = std::string("{\"id\":\"tree\",\"type\":\"session_tree\"}\n") + "{\"id\":\"switch\",\"type\":\"switch_session\",\"session_id\":\"" +
                        parent_id + "\"}\n" + "{\"id\":\"tree2\",\"type\":\"session_tree\"}\n";
  std::istringstream in(requests);
  std::ostringstream out;
  auto result = ava::app::run_rpc_loop(*child, open_options, provider, transport, ava::app::RuntimeRunOptions{}, in, out);
  auto const jsonl = out.str();
  expect(result.has_value(), "RPC session_tree loop completes successfully");
  expect(jsonl.find("\"id\":\"tree\"") != std::string::npos && jsonl.find("\"current_session_id\":\"" + child_id + "\"") != std::string::npos &&
             jsonl.find("\"path\":[\"" + parent_id + "\",\"" + child_id + "\"]") != std::string::npos &&
             jsonl.find("\"parent_session_id\":\"" + parent_id + "\"") != std::string::npos &&
             jsonl.find("\"children\":[\"" + child_id + "\"]") != std::string::npos && jsonl.find("\"labels\":[\"branch\"]") != std::string::npos &&
             jsonl.find("\"archived\":true") != std::string::npos &&
             jsonl.find("\"actor\":\"test\"") != std::string::npos,
         "RPC session_tree returns current path, children, labels, archive state, actor, and provenance metadata");
  expect(jsonl.find("\"id\":\"switch\"") != std::string::npos && jsonl.find("\"current_session_id\":\"" + parent_id + "\"") != std::string::npos &&
             child->store.session_id() == parent_id,
         "RPC switch_session navigates the active session used by following tree calls");
}

void test_app_rpc_session_fork_and_clone_commands()
{
  auto const root = temp_root() / "app-rpc-session-branch";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::RuntimeOpenOptions open_options;
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
  auto result = ava::app::run_rpc_loop(*session, open_options, provider, transport, ava::app::RuntimeRunOptions{}, in, out);
  auto const jsonl = out.str();
  auto source_store = ava::session::SessionStore::open(workspace, source_id, paths.sessions_dir);
  bool source_unchanged = false;
  if (source_store)
  {
    auto source_after = source_store->load();
    source_unchanged = source_after && source_after->size() == source_count;
  }
  expect(result.has_value(), "RPC fork/clone loop completes successfully");
  expect(source_unchanged, "RPC fork/clone commands do not write to the source session");
  expect(jsonl.find("\"id\":\"fork\"") != std::string::npos && jsonl.find("\"id\":\"fork_meta\"") != std::string::npos &&
             jsonl.find("\"created\":true") != std::string::npos && jsonl.find("\"name\":\"Forked\"") != std::string::npos &&
             jsonl.find("\"labels\":[\"forked\"]") != std::string::npos &&
             jsonl.find("\"parent_session_id\":\"" + source_id + "\"") != std::string::npos &&
             jsonl.find("\"branch_from_entry_id\":\"" + branch_from_entry_id + "\"") != std::string::npos &&
             jsonl.find("\"branch_origin\":\"fork\"") != std::string::npos,
         "RPC fork_session creates and switches to a fork with provenance metadata");
  expect(jsonl.find("\"id\":\"clone\"") != std::string::npos && jsonl.find("\"id\":\"clone_meta\"") != std::string::npos &&
             jsonl.find("\"name\":\"Cloned\"") != std::string::npos && jsonl.find("\"branch_origin\":\"clone\"") != std::string::npos,
         "RPC clone_session creates and switches to a clone with provenance metadata");
}

void test_app_rpc_summarize_branch_appends_to_source_session()
{
  auto const root = temp_root() / "app-rpc-branch-summary";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::RuntimeOpenOptions open_options;
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
  auto const requests = std::string("{\"id\":\"summary\",\"type\":\"summarize_branch\",") +
                        "\"branch_root_entry_id\":\"" + root_entry_id + "\","
                        "\"branch_tip_entry_id\":\"" + tip_entry_id + "\","
                        "\"summary\":\"Abandoned path was not needed.\","
                        "\"provider\":\"openai\",\"model\":\"gpt-test\",\"reason\":\"test\"}\n"
                        "{\"id\":\"stats\",\"type\":\"get_session_stats\"}\n";
  std::istringstream in(requests);
  std::ostringstream out;
  auto result = ava::app::run_rpc_loop(*session, open_options, provider, transport, ava::app::RuntimeRunOptions{}, in, out);
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
  expect(source_has_summary && session->store.session_id() == source_id, "RPC summarize_branch appends to the source session without switching sessions");
  expect(jsonl.find("\"id\":\"summary\"") != std::string::npos && jsonl.find("\"source_session_id\":\"" + source_id + "\"") != std::string::npos &&
             jsonl.find("\"type\":\"branch_summary\"") != std::string::npos && jsonl.find("Abandoned path was not needed.") != std::string::npos &&
             jsonl.find("\"branch_summary\":1") != std::string::npos,
         "RPC summarize_branch returns the persisted branch summary entry and updated stats expose the count");
}

void test_app_rpc_model_commands()
{
  auto const root = temp_root() / "app-rpc-model-commands";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::RuntimeOpenOptions open_options;
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
  auto result = ava::app::run_rpc_loop(*session, open_options, provider, transport, ava::app::RuntimeRunOptions{}, in, out);
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
  expect(jsonl.find("\"id\":\"cycle\"") != std::string::npos && jsonl.find("\"provider\":\"kimi\"") != std::string::npos,
         "RPC cycle_model advances to the next configured provider model");
  expect(session->model.provider_id == "kimi", "RPC cycle_model updates active session model");
  auto previous = ava::app::rpc::previous_runtime_model(*session);
  expect(previous && previous->provider_id == "anthropic" && previous->model_id == "claude-sonnet-4-5",
         "runtime previous model helper returns the configured predecessor for TUI reverse cycling");
}

void test_app_rpc_reasoning_commands()
{
  auto const root = temp_root() / "app-rpc-reasoning-commands";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::RuntimeOpenOptions open_options;
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
      "{\"id\":\"clear\",\"type\":\"clear_reasoning\"}\n");
  std::ostringstream out;
  auto result = ava::app::run_rpc_loop(*session, open_options, provider, transport, ava::app::RuntimeRunOptions{}, in, out);
  auto const jsonl = out.str();
  expect(result.has_value(), "RPC reasoning command loop completes successfully");
  expect(jsonl.find("\"id\":\"set\"") != std::string::npos && jsonl.find("\"reasoning_enabled\":true") != std::string::npos &&
             jsonl.find("\"reasoning_level\":\"medium\"") != std::string::npos,
         "RPC set_reasoning returns enabled reasoning state");
  expect(jsonl.find("\"id\":\"state\"") != std::string::npos && jsonl.find("\"reasoning_level\":\"medium\"") != std::string::npos,
         "RPC get_state reflects selected reasoning");
  expect(jsonl.find("\"id\":\"invalid\"") != std::string::npos && jsonl.find("reasoning level is not supported") != std::string::npos,
         "RPC set_reasoning reports invalid reasoning levels");
  expect(jsonl.find("\"id\":\"clear\"") != std::string::npos && jsonl.rfind("\"reasoning_enabled\":false") != std::string::npos,
         "RPC clear_reasoning disables reasoning state");
  expect(!session->reasoning, "RPC clear_reasoning updates active session state");

  auto entries = session->store.load();
  expect(entries.has_value(), "RPC reasoning command test reloads entries");
  if (entries)
  {
    auto const reasoning_changes = std::ranges::count_if(*entries, [](auto const& entry) { return entry.type == ava::session::EntryType::ReasoningChange; });
    expect(reasoning_changes == 2, "RPC reasoning commands persist set and clear reasoning_change entries");
  }
}

void test_app_rpc_protocol_version_and_session_commands()
{
  auto const root = temp_root() / "app-rpc-protocol-session";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::RuntimeOpenOptions open_options;
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
  auto appended_metadata = ava::session::append_session_metadata(session->store, std::move(metadata_update));

  auto appended_user = session->store.append(ava::session::SessionEntry{.id = "entry_user",
                                                                        .parent_id = "",
                                                                        .type = ava::session::EntryType::UserMessage,
                                                                        .timestamp = ava::session::now_timestamp(),
                                                                        .data_json = "{\"text\":\"hello\"}"});
  auto appended_internal_replay = session->store.append(ava::session::SessionEntry{.id = ava::core::make_id("entry"),
                                                                                   .parent_id = "",
                                                                                   .type = ava::session::EntryType::UserMessage,
                                                                                   .timestamp = ava::session::now_timestamp(),
                                                                                   .data_json = "{\"text\":\"hidden rpc replay\","
                                                                                                "\"internal_replay\":true,"
                                                                                                "\"replay_of\":\"entry_user\","
                                                                                                "\"reason\":\"test\"}"});
  auto appended_assistant = session->store.append(ava::session::SessionEntry{.id = "entry_assistant",
                                                                             .parent_id = "",
                                                                             .type = ava::session::EntryType::AssistantMessage,
                                                                             .timestamp = ava::session::now_timestamp(),
                                                                             .data_json = "{\"text\":\"answer\",\"usage\":{\"input_tokens\":1,"
                                                                                          "\"output_tokens\":1,\"total_tokens\":2,"
                                                                                          "\"cost_usd\":0.001,"
                                                                                          "\"source\":\"provider\"}}"});
  auto appended_unpriced_assistant = session->store.append(ava::session::SessionEntry{.id = ava::core::make_id("entry"),
                                                                                      .parent_id = "",
                                                                                      .type = ava::session::EntryType::AssistantMessage,
                                                                                      .timestamp = ava::session::now_timestamp(),
                                                                                      .data_json = "{\"text\":\"unknown cost\",\"usage\":{"
                                                                                                   "\"input_tokens\":1,\"cache_read_tokens\":1,"
                                                                                                   "\"total_tokens\":1,"
                                                                                                   "\"source\":\"provider\"}}"});
  auto appended_reasoning = session->store.append(ava::session::SessionEntry{.id = ava::core::make_id("entry"),
                                                                             .parent_id = "",
                                                                             .type = ava::session::EntryType::ReasoningBlock,
                                                                             .timestamp = ava::session::now_timestamp(),
                                                                             .data_json = "{\"provider\":\"anthropic\","
                                                                                          "\"model\":\"claude-sonnet-4-5\","
                                                                                          "\"format\":\"anthropic_thinking\","
                                                                                          "\"text\":\"visible reasoning\","
                                                                                          "\"signature\":\"rpc-secret-signature\"}"});
  auto appended_redacted_reasoning = session->store.append(ava::session::SessionEntry{.id = ava::core::make_id("entry"),
                                                                                      .parent_id = "",
                                                                                      .type = ava::session::EntryType::ReasoningBlock,
                                                                                      .timestamp = ava::session::now_timestamp(),
                                                                                      .data_json = "{\"provider\":\"anthropic\","
                                                                                                   "\"model\":\"claude-sonnet-4-5\","
                                                                                                   "\"format\":\"anthropic_thinking\","
                                                                                                   "\"text\":\"hidden redacted rpc reasoning\","
                                                                                                   "\"signature\":\"rpc-redacted-secret-signature\","
                                                                                                   "\"redacted\": true }"});
  auto appended_mode = session->store.append(ava::session::SessionEntry{.id = ava::core::make_id("entry"),
                                                                        .parent_id = "",
                                                                        .type = ava::session::EntryType::ModeChange,
                                                                        .timestamp = ava::session::now_timestamp(),
                                                                        .data_json = "{\"mode\":\"build\"}"});
  auto appended_compaction = session->store.append(ava::session::SessionEntry{.id = ava::core::make_id("entry"),
                                                                              .parent_id = "",
                                                                              .type = ava::session::EntryType::Compaction,
                                                                              .timestamp = ava::session::now_timestamp(),
                                                                              .data_json = "{\"summary\":\"prior\"}"});
  auto appended_cancel = session->store.append(ava::session::SessionEntry{.id = ava::core::make_id("entry"),
                                                                          .parent_id = "",
                                                                          .type = ava::session::EntryType::Cancel,
                                                                          .timestamp = ava::session::now_timestamp(),
                                                                          .data_json = "{}"});
  auto appended_branch_summary = session->store.append(ava::session::SessionEntry{.id = ava::core::make_id("entry"),
                                                                                   .parent_id = "",
                                                                                   .type = ava::session::EntryType::BranchSummary,
                                                                                   .timestamp = ava::session::now_timestamp(),
                                                                                    .data_json = "{\"schema_version\":1,\"source_session_id\":\"" + initial_id +
                                                                                                 "\",\"branch_root_entry_id\":\"entry_user\"," +
                                                                                                 "\"branch_tip_entry_id\":\"entry_assistant\","
                                                                                                "\"summary\":\"branch summary\",\"provider\":\"openai\"," +
                                                                                                "\"model\":\"gpt-test\",\"reason\":\"test\"}"});
  expect(appended_metadata.has_value() && appended_user.has_value() && appended_internal_replay.has_value() && appended_assistant.has_value() &&
             appended_unpriced_assistant.has_value() && appended_reasoning.has_value() && appended_redacted_reasoning.has_value() &&
             appended_mode.has_value() && appended_compaction.has_value() && appended_cancel.has_value() && appended_branch_summary.has_value(),
         "RPC protocol/session test appends messages and stats foundation entries");

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
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.access_token = "token";
  auto result = ava::app::run_rpc_loop(*session, open_options, provider, transport, runtime_options, in, out);
  auto const jsonl = out.str();
  auto const current_entry_version = std::string("\"version\":") + std::to_string(ava::session::kCurrentSessionEntryVersion);
  expect(result.has_value(), "RPC protocol/session loop completes successfully");
  expect(jsonl.find("\"id\":\"proto\"") != std::string::npos && jsonl.find("\"protocol_version\":1") != std::string::npos &&
             jsonl.find("\"supported_protocol_versions\":[1]") != std::string::npos && jsonl.find("\"session_entry_version\":3") != std::string::npos &&
             jsonl.find("\"supported_session_entry_versions\":[0,1,2,3]") != std::string::npos,
         "RPC get_protocol reports supported protocol and session entry versions");
  expect(jsonl.find("\"id\":\"messages\"") != std::string::npos && jsonl.find("\"messages\"") != std::string::npos &&
             jsonl.find(current_entry_version) != std::string::npos && jsonl.find("hello") != std::string::npos && jsonl.find("answer") != std::string::npos &&
             jsonl.find("visible reasoning") != std::string::npos && jsonl.find("hidden redacted rpc reasoning") == std::string::npos &&
             jsonl.find("\"signature_present\":true") != std::string::npos && jsonl.find("hidden rpc replay") == std::string::npos &&
             jsonl.find("rpc-secret-signature") == std::string::npos && jsonl.find("rpc-redacted-secret-signature") == std::string::npos,
         "RPC get_messages returns consumer-visible durable message entries without reasoning signatures");
  expect(jsonl.find("\"id\":\"stats\"") != std::string::npos && jsonl.find("\"entry_count\":12") != std::string::npos &&
             jsonl.find("\"session_metadata\":1") != std::string::npos && jsonl.find("\"user_message\":1") != std::string::npos &&
             jsonl.find("\"assistant_message\":2") != std::string::npos && jsonl.find("\"reasoning_block\":2") != std::string::npos &&
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
  expect(session->store.session_id() == initial_id && jsonl.find("\"id\":\"switch\"") != std::string::npos,
         "RPC switch_session switches back to the requested session");
}

void test_app_rpc_protocol_version_and_resolver_reply_errors()
{
  auto const root = temp_root() / "app-rpc-protocol-errors";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::RuntimeOpenOptions open_options;
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
  auto result = ava::app::run_rpc_loop(*session, open_options, provider, transport, ava::app::RuntimeRunOptions{}, in, out);
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

  auto const root = temp_root() / "app-rpc-mcp-commands";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  write_app_test_file(workspace / ".ava" / "mcp.json", app_test_mcp_config_json("demo", "Demo MCP", AVA_FAKE_MCP_SERVER_PATH));

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC MCP command test opens runtime session");
  if (!session)
    return;

  std::vector<ava::permissions::PermissionPrompt> prompts;
  ava::app::RuntimeRunOptions runtime_options;
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
  auto result = ava::app::run_rpc_loop(*session, open_options, provider, transport, runtime_options, in, out);
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
  auto const root = temp_root() / "app-rpc-commands";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  {
    std::ofstream file(workspace / "AGENTS.md", std::ios::binary | std::ios::trunc);
    file << "rpc command context\n";
  }
  write_app_test_file(workspace / ".ava" / "plugins" / "com.example.rpc" / "plugin.json", app_test_plugin_manifest_json("com.example.rpc", "RPC Plugin"));
  write_app_test_file(workspace / ".ava" / "plugins" / "com.example.rpcbad" / "plugin.json", "{not-json");

  ava::app::RuntimeOpenOptions open_options;
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
  std::istringstream in(
      "{\"id\":\"plugins\",\"type\":\"list_plugins\"}\n"
      "{\"id\":\"plugin-enable\",\"type\":\"enable_plugin\",\"plugin_id\":\"com.example.rpc\"}\n"
      "{\"id\":\"plugin-inspect\",\"type\":\"inspect_plugin\",\"plugin_id\":\"com.example.rpc\"}\n"
      "{\"id\":\"plugin-validate\",\"type\":\"validate_plugin\",\"path\":\".ava/plugins/com.example.rpc/"
      "plugin.json\"}\n"
      "{\"id\":\"plugin-failures\",\"type\":\"plugin_failures\"}\n"
      "{\"id\":\"ctx\",\"type\":\"context\"}\n"
      "{\"id\":\"read\",\"type\":\"invoke_command\",\"name\":\"read\",\"command_arguments\":\"AGENTS.md\"}\n"
      "{\"id\":\"cmp\",\"type\":\"compact\",\"instructions\":\"remember rpc facts\"}\n"
      "{\"id\":\"exp\",\"type\":\"export\"}\n");
  std::ostringstream out;
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.access_token = "token";
  auto result = ava::app::run_rpc_loop(*session, open_options, provider, transport, runtime_options, in, out);
  auto const jsonl = out.str();
  expect(result.has_value(), "RPC context/compact/export loop completes successfully");
  expect(jsonl.find("\"id\":\"plugins\"") != std::string::npos && jsonl.find("com.example.rpc") != std::string::npos &&
             jsonl.find("\"id\":\"plugin-enable\"") != std::string::npos && jsonl.find("No plugin process was started") != std::string::npos &&
             jsonl.find("\"id\":\"plugin-inspect\"") != std::string::npos && jsonl.find("status: enabled") != std::string::npos &&
             jsonl.find("\"id\":\"plugin-validate\"") != std::string::npos && jsonl.find("Valid plugin manifest") != std::string::npos &&
             jsonl.find("\"id\":\"plugin-failures\"") != std::string::npos && jsonl.find("com.example.rpcbad") != std::string::npos &&
             jsonl.find("\"id\":\"ctx\"") != std::string::npos && jsonl.find("AGENTS.md") != std::string::npos &&
             jsonl.find("\"id\":\"read\"") != std::string::npos && jsonl.find("\"tool_timeline\"") != std::string::npos &&
             jsonl.find("\"structured_result\":{\"schema_version\":1") != std::string::npos &&
             jsonl.find("\"tool\":\"read\"") != std::string::npos &&
             jsonl.find("\"id\":\"cmp\"") != std::string::npos && jsonl.find("\"name\":\"compaction_start\"") != std::string::npos &&
             jsonl.find("\"name\":\"compaction_end\"") != std::string::npos && jsonl.find("compaction summary recorded") != std::string::npos &&
             jsonl.find("\"id\":\"exp\"") != std::string::npos && jsonl.find("# AVA Session Export") != std::string::npos &&
             jsonl.find("remember rpc facts") != std::string::npos,
         "RPC command responses expose command dispatcher output as JSONL protocol records");
}

void test_app_rpc_compact_provider_failure_is_error_response()
{
  auto const root = temp_root() / "app-rpc-compact-failure";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC compact failure test opens runtime session");
  if (!session)
    return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({ava::provider::HttpResponse{.status_code = 500, .headers = {}, .body = "{\"error\":{\"message\":\"summary failed\"}}"}});
  std::istringstream in("{\"id\":\"cmp-fail\",\"type\":\"compact\"}\n");
  std::ostringstream out;
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.access_token = "token";

  auto result = ava::app::run_rpc_loop(*session, open_options, provider, transport, runtime_options, in, out);
  auto const jsonl = out.str();
  auto entries = session->store.load();
  expect(result.has_value(), "RPC compact failure loop completes after error response");
  expect(jsonl.find("\"id\":\"cmp-fail\"") != std::string::npos && jsonl.find("\"success\":false") != std::string::npos &&
             jsonl.find("compaction summary request failed with status 500") != std::string::npos && jsonl.find("summary failed") != std::string::npos,
         "RPC compact provider failures are machine-readable error responses");
  expect(entries && count_compaction_entries(*entries) == 0, "RPC compact provider failure leaves session without a compaction entry");
}

void test_app_rpc_compact_cancellation_is_error_response_without_provider_request()
{
  auto const root = temp_root() / "app-rpc-compact-canceled";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "RPC compact cancellation test opens runtime session");
  if (!session)
    return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport(
      {ava::provider::HttpResponse{.status_code = 200, .headers = {}, .body = "{\"output_text\":\"unused\"}"}});
  std::istringstream in("{\"id\":\"cmp-cancel\",\"type\":\"compact\"}\n");
  std::ostringstream out;
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.access_token = "token";
  runtime_options.cancel_requested = [] { return true; };

  auto result = ava::app::run_rpc_loop(*session, open_options, provider, transport, runtime_options, in, out);
  auto const jsonl = out.str();
  auto entries = session->store.load();
  expect(result.has_value(), "RPC compact cancellation loop completes after error response");
  expect(jsonl.find("\"id\":\"cmp-cancel\"") != std::string::npos &&
             jsonl.find("\"success\":false") != std::string::npos &&
             jsonl.find("agent loop canceled") != std::string::npos,
         "RPC compact cancellation is returned as a machine-readable error response");
  expect(transport.requests().empty(), "RPC compact cancellation avoids dispatching a provider summary request");
  expect(entries && count_compaction_entries(*entries) == 0,
         "RPC compact cancellation leaves session without a compaction entry");
}

}  // namespace

void run_app_rpc_tests()
{
  test_app_rpc_prompt_payload_serialization();
  test_app_rpc_prompt_result_tool_timeline_golden_payloads();
  test_app_rpc_parsing_and_response_serialization();
  test_app_rpc_identifier_validation();
  test_app_rpc_prompt_with_fake_transport_streams_events();
  test_app_rpc_prompt_streams_provider_deltas_before_final_response();
  test_app_rpc_prompt_retry_transport_cancellation_is_canceled_event();
  test_app_rpc_prompt_after_idle_cancel_clears_cancel_flag();
  test_app_rpc_prompt_refreshes_expired_oauth_before_provider_request();
  test_app_rpc_malformed_line_recovery_and_unknown_command();
  test_app_rpc_state_list_sessions_and_open_session();
  test_app_rpc_session_metadata_name_and_labels();
  test_app_rpc_session_tree_command_and_switch_navigation();
  test_app_rpc_session_fork_and_clone_commands();
  test_app_rpc_summarize_branch_appends_to_source_session();
  test_app_rpc_model_commands();
  test_app_rpc_reasoning_commands();
  test_app_rpc_protocol_version_and_session_commands();
  test_app_rpc_protocol_version_and_resolver_reply_errors();
  test_app_rpc_mcp_command_responses();
  test_app_rpc_command_responses_for_context_compact_export();
  test_app_rpc_compact_provider_failure_is_error_response();
  test_app_rpc_compact_cancellation_is_error_response_without_provider_request();
}
