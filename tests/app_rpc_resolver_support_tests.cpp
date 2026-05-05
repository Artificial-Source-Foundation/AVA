#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "ava/app/rpc/resolver_support.h"
#include "tests/support/test_harness.h"

namespace {

ava::app::rpc::PendingPermissionRequest permission_request()
{
  return ava::app::rpc::PendingPermissionRequest{.resolved = false,
                                                 .correlation_id = "corr-1",
                                                 .permission_request_id = "perm-1",
                                                 .operation = ava::permissions::Operation::EditFile,
                                                 .mode = ava::agent::Mode::Build,
                                                 .tool_name = "write_file",
                                                 .target_path = std::filesystem::path("/tmp/file.txt"),
                                                 .command = "",
                                                 .reason = "needs write",
                                                 .risk = ava::permissions::PermissionRisk::High,
                                                 .resolution = std::nullopt,
                                                 .error = std::nullopt};
}

ava::permissions::PermissionPrompt permission_prompt()
{
  return ava::permissions::PermissionPrompt{.permission_request_id = "perm-1",
                                            .operation = ava::permissions::Operation::EditFile,
                                            .mode = ava::agent::Mode::Build,
                                            .workspace_dir = "/tmp",
                                            .target_path = "/tmp/file.txt",
                                            .command = "",
                                            .tool_name = "write_file",
                                            .reason = "needs write",
                                            .risk = ava::permissions::PermissionRisk::High,
                                            .diff_preview = {},
                                            .diff_truncated = false};
}

void test_permission_grant_helpers()
{
  auto request = permission_request();
  auto grant = ava::app::rpc::detail::grant_from_request(request);
  expect(grant.grant_id.rfind("permgrant_", 0) == 0 && grant.permission_request_id == "perm-1" &&
             grant.operation == request.operation && grant.mode == request.mode && grant.tool_name == "write_file" &&
             grant.target_path == "/tmp/file.txt" && grant.reason == "needs write" &&
             grant.risk == ava::permissions::PermissionRisk::High,
         "RPC resolver support creates session grants from pending permission requests");

  auto prompt = permission_prompt();
  expect(ava::app::rpc::detail::grant_matches(grant, request) && ava::app::rpc::detail::grant_matches(grant, prompt),
         "RPC resolver support matches grants against equivalent permission requests and prompts");

  prompt.target_path = "/tmp/other.txt";
  expect(!ava::app::rpc::detail::grant_matches(grant, prompt),
         "RPC resolver support rejects grants for different permission targets");
}

void test_permission_grant_json()
{
  auto grant = ava::app::rpc::detail::grant_from_request(permission_request());
  grant.grant_id = "grant\"1";
  grant.command = "printf \"ok\"";
  auto json = ava::app::rpc::detail::permission_session_grant_json(grant);
  expect(json.find("\"grant_id\":\"grant\\\"1\"") != std::string::npos &&
             json.find("\"operation\":\"edit\"") != std::string::npos &&
             json.find("\"mode\":\"build\"") != std::string::npos &&
             json.find("\"command\":\"printf \\\"ok\\\"\"") != std::string::npos &&
             json.find("\"risk\":\"high\"") != std::string::npos,
         "RPC resolver support serializes escaped permission session grants");
}

void test_permission_reply_decisions()
{
  auto allow = ava::app::rpc::detail::parse_permission_reply_decision("allow");
  auto allow_session = ava::app::rpc::detail::parse_permission_reply_decision("allow_session");
  auto deny = ava::app::rpc::detail::parse_permission_reply_decision("deny");
  auto bad = ava::app::rpc::detail::parse_permission_reply_decision("maybe");

  expect(allow && allow->resolution == ava::permissions::PermissionResolution::Allow && !allow->create_session_grant,
         "RPC resolver support parses allow permission replies");
  expect(allow_session && allow_session->resolution == ava::permissions::PermissionResolution::Allow &&
             allow_session->create_session_grant,
         "RPC resolver support parses allow-session permission replies");
  expect(deny && deny->resolution == ava::permissions::PermissionResolution::Deny && !deny->create_session_grant,
         "RPC resolver support parses deny permission replies");
  expect(!bad && bad.error().message().find("permission_reply decision") != std::string::npos,
         "RPC resolver support rejects invalid permission replies");
}

void test_question_reply_parsing()
{
  ava::app::rpc::PendingQuestionRequest pending{.resolved = false,
                                                .correlation_id = "corr-question",
                                                .allow_custom = true,
                                                .options = {ava::agent::QuestionOption{.value = "a", .label = "A"},
                                                            ava::agent::QuestionOption{.value = "b", .label = "B"}},
                                                .answer = std::nullopt,
                                                .error = std::nullopt};

  auto custom = ava::app::rpc::detail::parse_question_reply(pending, std::string("custom answer"), std::nullopt);
  expect(custom && custom->custom_text == "custom answer" && custom->selected_options.empty(),
         "RPC resolver support parses custom question answers");

  auto selected = ava::app::rpc::detail::parse_question_reply(pending, std::nullopt, std::string("b"));
  expect(selected && selected->selected_options.size() == 1 && selected->selected_options[0] == "b" &&
             selected->custom_text.empty(),
         "RPC resolver support parses selected question options");

  auto duplicate = ava::app::rpc::detail::parse_question_reply(pending, std::string("custom"), std::string("a"));
  expect(!duplicate && duplicate.error().message().find("answer or selected, not both") != std::string::npos,
         "RPC resolver support rejects duplicate question reply payloads");

  auto invalid_selected = ava::app::rpc::detail::parse_question_reply(pending, std::nullopt, std::string("missing"));
  expect(!invalid_selected && invalid_selected.error().message().find("selected option") != std::string::npos,
         "RPC resolver support rejects unknown selected options");

  pending.allow_custom = false;
  auto custom_not_allowed = ava::app::rpc::detail::parse_question_reply(pending, std::string("custom"), std::nullopt);
  expect(!custom_not_allowed && custom_not_allowed.error().message().find("answer is not allowed") != std::string::npos,
         "RPC resolver support rejects custom answers when custom text is disabled");

  auto missing = ava::app::rpc::detail::parse_question_reply(pending, std::nullopt, std::nullopt);
  expect(!missing && missing.error().message().find("requires answer or selected") != std::string::npos,
         "RPC resolver support rejects empty question replies");
}

void test_request_id_and_missing_request_error()
{
  auto request_id = ava::app::rpc::detail::next_resolver_request_id("permission");
  auto error = ava::app::rpc::detail::no_pending_request_error("missing-id");
  expect(request_id.rfind("permission_", 0) == 0, "RPC resolver support creates prefixed resolver request IDs");
  expect(error.message().find("no matching pending request") != std::string::npos && !error.context().empty() &&
             error.context()[0].value == "missing-id",
         "RPC resolver support creates contextual no-pending-request errors");
}

}  // namespace

void run_app_rpc_resolver_support_tests()
{
  test_permission_grant_helpers();
  test_permission_grant_json();
  test_permission_reply_decisions();
  test_question_reply_parsing();
  test_request_id_and_missing_request_error();
}
