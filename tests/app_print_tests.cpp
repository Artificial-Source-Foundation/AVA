#include "ava/app/connect_openai.h"
#include "ava/app/headless_policy.h"
#include "ava/app/print_mode.h"
#include "ava/app/runtime.h"

#include "ava/config/auth.h"
#include "ava/config/openai_oauth.h"

#include "ava/permissions/permission.h"
#include "ava/permissions/permission_rules.h"

#include "ava/provider/openai_provider.h"

#include "ava/core/json.h"

#include "tests/support/app_runtime_support.h"
#include "tests/support/fake_transport.h"
#include "tests/support/test_harness.h"

#include <climits>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>

#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

namespace {

using namespace ava::tests;

// ScopedStdinTerminalState snapshots the process stdin terminal attributes for
// tests that deliberately exercise interactive code with synthetic streams
// while still reporting `stdin_is_tty=true`.  It has no inputs beyond
// STDIN_FILENO, produces no values, and restores the saved attributes with
// TCSANOW when explicitly requested or when destroyed.  If the test is run
// without a real terminal, tcgetattr fails and the guard becomes a no-op.
class ScopedStdinTerminalState {
 public:
  ScopedStdinTerminalState() : active_(::tcgetattr(STDIN_FILENO, &original_) == 0) {}

  ScopedStdinTerminalState(ScopedStdinTerminalState const&) = delete;
  ScopedStdinTerminalState& operator=(ScopedStdinTerminalState const&) = delete;

  ~ScopedStdinTerminalState() { restore(); }

  void restore() noexcept
  {
    if (!active_) return;
    static_cast<void>(::tcsetattr(STDIN_FILENO, TCSANOW, &original_));
    active_ = false;
  }

 private:
  termios original_{};
  bool active_ = false;
};

void test_app_print_prompt_merging()
{
  auto explicit_only = ava::app::merge_print_prompt(
      ava::app::PrintPromptInputs{.explicit_prompt = std::string("explicit"), .stdin_prompt = std::nullopt});
  expect(explicit_only && *explicit_only == "explicit", "print prompt uses explicit prompt when stdin is absent");

  auto stdin_only = ava::app::merge_print_prompt(
      ava::app::PrintPromptInputs{.explicit_prompt = std::nullopt, .stdin_prompt = std::string("stdin")});
  expect(stdin_only && *stdin_only == "stdin", "print prompt uses stdin when explicit prompt is absent");

  auto merged = ava::app::merge_print_prompt(
      ava::app::PrintPromptInputs{.explicit_prompt = std::string("explicit"), .stdin_prompt = std::string("stdin")});
  expect(merged && *merged == "explicit\n\nstdin", "print prompt merges explicit and stdin prompts deterministically");

  auto missing = ava::app::merge_print_prompt(
      ava::app::PrintPromptInputs{.explicit_prompt = std::nullopt, .stdin_prompt = std::nullopt});
  expect(!missing && missing.error().message().find("requires a prompt") != std::string::npos,
         "print prompt rejects missing prompt input");
}

void test_headless_permission_policy()
{
  auto const workspace = temp_root() / "headless-policy" / "workspace";
  auto const outside = temp_root() / "headless-policy" / "outside.txt";

  ava::permissions::PermissionPrompt const read_prompt{.operation = ava::permissions::Operation::ReadFile,
                                                       .mode = ava::agent::Mode::Build,
                                                       .workspace_dir = workspace,
                                                       .target_path = outside,
                                                       .command = "",
                                                       .tool_name = "read_file",
                                                       .reason = "target is outside the workspace"};
  ava::permissions::PermissionPrompt const search_prompt{.operation = ava::permissions::Operation::SearchFiles,
                                                         .mode = ava::agent::Mode::Build,
                                                         .workspace_dir = workspace,
                                                         .target_path = workspace,
                                                         .command = "",
                                                         .tool_name = "glob",
                                                         .reason = "search requires approval"};
  ava::permissions::PermissionPrompt const write_prompt{.operation = ava::permissions::Operation::EditFile,
                                                        .mode = ava::agent::Mode::Build,
                                                        .workspace_dir = workspace,
                                                        .target_path = outside,
                                                        .command = "",
                                                        .tool_name = "write_file",
                                                        .reason = "target is outside the workspace"};
  ava::permissions::PermissionPrompt const bash_prompt{.operation = ava::permissions::Operation::RunCommand,
                                                       .mode = ava::agent::Mode::Build,
                                                       .workspace_dir = workspace,
                                                       .target_path = workspace,
                                                       .command = "true",
                                                       .tool_name = "bash",
                                                       .reason = "command risk is unknown"};
  ava::permissions::PermissionPrompt const webfetch_prompt{.operation = ava::permissions::Operation::NetworkFetch,
                                                           .mode = ava::agent::Mode::Build,
                                                           .workspace_dir = workspace,
                                                           .target_path = {},
                                                           .command = "https://example.com",
                                                           .tool_name = "webfetch",
                                                           .reason = "network fetch requires explicit approval"};
  ava::permissions::PermissionPrompt const websearch_prompt{.operation = ava::permissions::Operation::NetworkSearch,
                                                            .mode = ava::agent::Mode::Build,
                                                            .workspace_dir = workspace,
                                                            .target_path = {},
                                                            .command = "current docs",
                                                            .tool_name = "websearch",
                                                            .reason = "network search requires explicit approval"};
  ava::permissions::PermissionPrompt const skill_prompt{
      .operation = ava::permissions::Operation::SkillLoad,
      .mode = ava::agent::Mode::Build,
      .workspace_dir = workspace,
      .target_path = workspace / ".ava" / "skills" / "demo" / "SKILL.md",
      .command = "demo",
      .tool_name = "skill",
      .reason = "skill loading requires explicit approval"};
  ava::permissions::PermissionPrompt const mcp_prompt{.operation = ava::permissions::Operation::McpToolCall,
                                                      .mode = ava::agent::Mode::Build,
                                                      .workspace_dir = workspace,
                                                      .target_path = workspace / ".ava" / "mcp.json",
                                                      .command = "demo:echo",
                                                      .tool_name = "mcp_demo_echo",
                                                      .reason = "MCP tool calls require explicit approval"};

  auto default_resolver = ava::app::build_headless_permission_resolver(ava::app::HeadlessPermissionPolicyOptions{});
  auto default_read = default_resolver(read_prompt);
  expect(default_read && *default_read == ava::permissions::PermissionResolution::Deny,
         "headless default resolver denies Ask prompts");

  ava::app::HeadlessPermissionPolicyOptions read_only_options;
  auto read_only_added = ava::app::add_headless_allow_policy(read_only_options, "read-only");
  expect(read_only_added.has_value(), "headless read-only allow value parses");
  auto read_only_resolver = ava::app::build_headless_permission_resolver(read_only_options);
  auto read_only_read = read_only_resolver(read_prompt);
  auto read_only_search = read_only_resolver(search_prompt);
  auto read_only_write = read_only_resolver(write_prompt);
  auto read_only_bash = read_only_resolver(bash_prompt);
  auto read_only_webfetch = read_only_resolver(webfetch_prompt);
  auto read_only_websearch = read_only_resolver(websearch_prompt);
  expect(read_only_read && *read_only_read == ava::permissions::PermissionResolution::Allow,
         "headless read-only policy allows read prompts");
  expect(read_only_search && *read_only_search == ava::permissions::PermissionResolution::Allow,
         "headless read-only policy allows search prompts");
  expect(read_only_write && *read_only_write == ava::permissions::PermissionResolution::Deny,
         "headless read-only policy denies write prompts");
  expect(read_only_bash && *read_only_bash == ava::permissions::PermissionResolution::Deny,
         "headless read-only policy denies bash prompts");
  expect(read_only_webfetch && *read_only_webfetch == ava::permissions::PermissionResolution::Deny,
         "headless read-only policy denies network prompts");
  expect(read_only_websearch && *read_only_websearch == ava::permissions::PermissionResolution::Deny,
         "headless read-only policy denies network search prompts");

  ava::app::HeadlessPermissionPolicyOptions tool_options;
  auto tools_added =
      ava::app::add_headless_allowed_tools(tool_options, "glob,grep,mcp,read_file,skill,webfetch,websearch");
  expect(tools_added.has_value() && tool_options.allowed_tools.size() == 7,
         "headless allow-tool parses supported comma-separated tool names");
  auto tool_resolver = ava::app::build_headless_permission_resolver(tool_options);
  auto const tool_read = tool_resolver(read_prompt);
  auto const tool_search = tool_resolver(search_prompt);
  auto const tool_webfetch = tool_resolver(webfetch_prompt);
  auto const tool_websearch = tool_resolver(websearch_prompt);
  auto const tool_skill = tool_resolver(skill_prompt);
  auto const tool_mcp = tool_resolver(mcp_prompt);
  ava::permissions::PermissionPrompt const lower_layer_read_prompt{.operation = ava::permissions::Operation::ReadFile,
                                                                   .mode = ava::agent::Mode::Build,
                                                                   .workspace_dir = workspace,
                                                                   .target_path = outside,
                                                                   .command = "",
                                                                   .tool_name = "read",
                                                                   .reason = "target is outside the workspace"};
  ava::permissions::PermissionPrompt const mismatched_tool_prompt{.operation = ava::permissions::Operation::EditFile,
                                                                  .mode = ava::agent::Mode::Build,
                                                                  .workspace_dir = workspace,
                                                                  .target_path = outside,
                                                                  .command = "",
                                                                  .tool_name = "read_file",
                                                                  .reason = "target is outside the workspace"};
  auto const lower_layer_read = tool_resolver(lower_layer_read_prompt);
  auto const mismatched_tool = tool_resolver(mismatched_tool_prompt);
  expect(tool_read && *tool_read == ava::permissions::PermissionResolution::Allow,
         "headless allow-tool allows exact read_file prompts");
  expect(tool_search && *tool_search == ava::permissions::PermissionResolution::Allow,
         "headless allow-tool allows exact glob search prompts");
  expect(tool_webfetch && *tool_webfetch == ava::permissions::PermissionResolution::Allow,
         "headless allow-tool allows exact webfetch network prompts");
  expect(tool_websearch && *tool_websearch == ava::permissions::PermissionResolution::Allow,
         "headless allow-tool allows exact websearch network prompts");
  expect(tool_skill && *tool_skill == ava::permissions::PermissionResolution::Allow,
         "headless allow-tool allows exact skill prompts");
  expect(tool_mcp && *tool_mcp == ava::permissions::PermissionResolution::Allow,
         "headless allow-tool allows dynamic MCP tool prompts through the mcp group");
  expect(lower_layer_read && *lower_layer_read == ava::permissions::PermissionResolution::Deny,
         "headless allow-tool requires exact tool names");
  expect(mismatched_tool && *mismatched_tool == ava::permissions::PermissionResolution::Deny,
         "headless allow-tool does not allow unsafe operations with a safe tool name");

  auto invalid_allow = ava::app::add_headless_allow_policy(tool_options, "nope");
  auto invalid_tool = ava::app::add_headless_allowed_tools(tool_options, "glob,nope");
  auto empty_tool = ava::app::add_headless_allowed_tools(tool_options, "glob,");
  expect(!invalid_allow && invalid_allow.error().category() == ava::core::ErrorCategory::InvalidArgument,
         "headless --allow rejects unsupported values");
  expect(!invalid_tool && invalid_tool.error().category() == ava::core::ErrorCategory::InvalidArgument,
         "headless --allow-tool rejects unsupported values");
  expect(!empty_tool && empty_tool.error().category() == ava::core::ErrorCategory::InvalidArgument,
         "headless --allow-tool rejects empty tool names");
}

void test_app_print_text_mode_outputs_final_text_only()
{
  auto const root = temp_root() / "app-print-text";
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
  expect(session.has_value(), "print text test opens runtime session");
  if (!session) return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({ava::provider::HttpResponse{
      .status_code = 200,
      .headers = {},
      .body = "data: {\"type\":\"response.output_text.delta\",\"delta\":\"print answer\"}\n\n"
              "data: [DONE]\n\n",
  }});
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.access_token = "token";
  ava::app::PrintModeRunOptions const run_options{.output_format = ava::app::PrintOutputFormat::Text,
                                                  .runtime_options = runtime_options};
  std::ostringstream out;
  std::ostringstream err;
  auto result = ava::app::run_print_prompt(*session, "hello print", provider, transport, run_options, out, err);
  expect(result && result->final_text == "print answer", "print text mode returns agent result");
  expect(out.str() == "print answer" && err.str().empty(), "print text mode writes only final text to stdout");
  expect(transport.requests().size() == 1 && transport.requests()[0].body.find("hello print") != std::string::npos,
         "print text mode sends prompt through shared runtime");
}

void test_app_print_text_mode_with_streaming_keeps_stdout_final_only()
{
  auto const root = temp_root() / "app-print-text-streaming";
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
  expect(session.has_value(), "print text streaming test opens runtime session");
  if (!session) return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ChunkedStreamingTransport transport({"data: {\"type\":\"response.output_text.delta\",\"delta\":\"live \"}\n\n",
                                       "data: {\"type\":\"response.output_text.delta\",\"delta\":\"answer\"}\n\n",
                                       "data: [DONE]\n\n"});
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.access_token = "token";
  ava::app::PrintModeRunOptions const run_options{.output_format = ava::app::PrintOutputFormat::Text,
                                                  .runtime_options = runtime_options};
  std::ostringstream out;
  std::ostringstream err;
  auto result =
      ava::app::run_print_prompt(*session, "hello streaming print", provider, transport, run_options, out, err);
  expect(result && result->final_text == "live answer", "print text streaming mode returns final agent result");
  expect(out.str() == "live answer" && err.str().empty(), "print text streaming mode keeps stdout final-answer-only");
}

void test_app_print_text_mode_reports_stdout_write_failure()
{
  auto const root = temp_root() / "app-print-text-write-failure";
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
  expect(session.has_value(), "print text stdout failure test opens runtime session");
  if (!session) return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({ava::provider::HttpResponse{
      .status_code = 200,
      .headers = {},
      .body = "data: {\"type\":\"response.output_text.delta\",\"delta\":\"print answer\"}\n\n"
              "data: [DONE]\n\n",
  }});
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.access_token = "token";
  ava::app::PrintModeRunOptions const run_options{.output_format = ava::app::PrintOutputFormat::Text,
                                                  .runtime_options = runtime_options};
  FailingStreambuf failing_buffer;
  std::ostream out(&failing_buffer);
  std::ostringstream err;
  auto result = ava::app::run_print_prompt(*session, "hello print", provider, transport, run_options, out, err);
  expect(!result && result.error().category() == ava::core::ErrorCategory::Io &&
             result.error().message() == "failed to write print output",
         "print text mode reports stdout write failures");
}

void test_app_print_mode_uses_headless_permission_policy()
{
  auto const root = temp_root() / "app-print-policy";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const outside_path = root / "outside.txt";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  {
    std::ofstream file(outside_path, std::ios::binary | std::ios::trunc);
    file << "outside print policy";
  }

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.mode = ava::agent::Mode::Build;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "print policy test opens runtime session");
  if (!session) return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({ava::provider::HttpResponse{
                                           .status_code = 200,
                                           .headers = {},
                                           .body = "data: {\"type\":\"response.function_call.added\",\"item_id\":"
                                                   "\"call_outside\",\"name\":\"read_file\"}\n\n"
                                                   "data: "
                                                   "{\"type\":\"response.function_call_arguments.delta\","
                                                   "\"item_id\":\"call_outside\",\"delta\":\"{"
                                                   "\\\"path\\\":\\\"" +
                                                   ava::core::json::escape(outside_path.generic_string()) +
                                                   "\\\"}\"}\n\n"
                                                   "data: [DONE]\n\n",
                                       },
                                       ava::provider::HttpResponse{
                                           .status_code = 200,
                                           .headers = {},
                                           .body = "data: {\"type\":\"response.output_text.delta\",\"delta\":"
                                                   "\"policy allowed\"}\n\n"
                                                   "data: [DONE]\n\n",
                                       }});

  ava::app::HeadlessPermissionPolicyOptions policy_options;
  auto allowed_tool = ava::app::add_headless_allowed_tools(policy_options, "read_file");
  expect(allowed_tool.has_value(), "print policy test configures read_file allow-tool");
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.access_token = "token";
  runtime_options.permission_resolver = ava::app::build_headless_permission_resolver(policy_options);
  ava::app::PrintModeRunOptions const run_options{.output_format = ava::app::PrintOutputFormat::Text,
                                                  .runtime_options = std::move(runtime_options)};
  std::ostringstream out;
  std::ostringstream err;
  auto result =
      ava::app::run_print_prompt(*session, "read outside in print", provider, transport, run_options, out, err);
  expect(result && result->final_text == "policy allowed" && result->tool_calls == 1,
         "print mode uses supplied headless permission resolver for tool asks");
  expect(transport.requests().size() == 2 &&
             transport.requests()[1].body.find("outside print policy") != std::string::npos,
         "print mode continuation includes allow-tool-approved read_file result");
}

void test_app_print_mode_uses_persistent_permission_rules()
{
  auto const root = temp_root() / "app-print-persistent-permission-rule";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  auto const outside_path = root / "outside-print-rule.txt";
  write_app_test_file(outside_path, "outside print persistent rule note");

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "print persistent permission rule test opens runtime session");
  if (!session) return;

  ava::permissions::PermissionRuleStore const store{
      .global_rules_file = paths.ava_config_dir / "permission-rules.json",
      .workspace_rules_file = workspace / ".ava" / "permission-rules.json",
      .workspace_dir = workspace,
  };
  auto added = ava::permissions::add_persistent_permission_rule(
      store, ava::permissions::PermissionRuleDraft{.scope = ava::permissions::PermissionRuleScope::Workspace,
                                                   .action = ava::permissions::PermissionAction::Allow,
                                                   .operation = ava::permissions::Operation::ReadFile,
                                                   .mode = ava::permissions::PermissionRuleMode::Any,
                                                   .tool_name = "",
                                                   .target_path = outside_path,
                                                   .command = "",
                                                   .reason = "allow exact print outside read",
                                                   .actor = "test"});
  expect(added.has_value(), "print persistent permission rule test stores allow rule");
  if (!added) return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({sse_response(read_file_call_sse(outside_path.generic_string())),
                                       sse_response(final_text_sse("persistent print allowed"))});
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.access_token = "token";
  ava::app::PrintModeRunOptions const run_options{.output_format = ava::app::PrintOutputFormat::Text,
                                                  .runtime_options = std::move(runtime_options)};
  std::ostringstream out;
  std::ostringstream err;
  auto result = ava::app::run_print_prompt(*session, "read outside with persistent print rule", provider, transport,
                                           run_options, out, err);

  auto entries = session->store.load();
  auto audits = entries ? permission_entries(*entries) : std::vector<ava::session::SessionEntry>{};
  bool persistent_audited = false;
  for (auto const& audit : audits) {
    persistent_audited = persistent_audited ||
                         (ava::core::json::string_field(audit.data_json, "resolution_source") == "persistent_rule" &&
                          ava::core::json::string_field(audit.data_json, "rule_id") == added->rule_id);
  }
  expect(result && result->final_text == "persistent print allowed" && result->tool_calls == 1,
         "print mode applies matching persistent permission rules before deny fallback");
  expect(persistent_audited, "print mode persistent permission decisions are audited with the matching rule id");
}

void test_app_print_mode_refreshes_expired_oauth_before_provider_request()
{
  auto const root = temp_root() / "app-print-oauth-refresh";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  auto stored = ava::config::store_openai_credential(
      paths, ava::config::OpenAICredential{.type = ava::config::OpenAICredentialType::OAuth,
                                           .access_token = "expired-print-access",
                                           .refresh_token = "print-refresh",
                                           .expires_at = 100,
                                           .account_id = "acct_old",
                                           .source_path = {}});
  expect(stored.has_value(), "print OAuth refresh test stores expired credential");

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({ava::provider::HttpResponse{
                                           .status_code = 200,
                                           .headers = {},
                                           .body = "{\"access_token\":\"print-refreshed-access\","
                                                   "\"refresh_token\":\"print-rotated-refresh\","
                                                   "\"expires_in\":3600,\"account_id\":\"acct_print\"}",
                                       },
                                       ava::provider::HttpResponse{
                                           .status_code = 200,
                                           .headers = {},
                                           .body = "data: {\"type\":\"response.output_text.delta\",\"delta\":"
                                                   "\"print refreshed answer\"}\n\n"
                                                   "data: [DONE]\n\n",
                                       }});

  ava::app::PrintModeOptions options;
  options.open_options.workspace_dir = workspace;
  options.open_options.current_dir = workspace;
  options.open_options.mode = ava::agent::Mode::Build;
  options.open_options.paths = paths;
  options.explicit_prompt = "hello refreshed print";
  options.provider_override = std::cref(provider);
  options.transport_override = std::ref(transport);

  std::istringstream in;
  std::ostringstream out;
  std::ostringstream err;
  auto const exit_code = ava::app::run_print_mode(options, in, out, err);
  expect(exit_code == 0 && out.str() == "print refreshed answer" && err.str().empty(),
         "print mode completes after refreshing expired OAuth credentials");
  expect(transport.requests().size() == 2 && transport.requests()[0].url == "https://auth.openai.com/oauth/token" &&
             transport.requests()[1].headers.at("Authorization") == "Bearer print-refreshed-access" &&
             transport.requests()[1].headers.at("ChatGPT-Account-Id") == "acct_print" &&
             transport.requests()[1].body.find("hello refreshed print") != std::string::npos,
         "print mode refreshes OAuth before sending provider request");
  auto persisted = ava::config::load_openai_credential(paths);
  expect(persisted && persisted->has_value() && (*persisted)->access_token == "print-refreshed-access" &&
             (*persisted)->refresh_token == "print-rotated-refresh",
         "print mode OAuth preflight persists refreshed credential before provider startup");
}

void test_app_connect_provider_credentials_headlessly()
{
  ScopedStdinTerminalState terminal_state;
  auto const root = temp_root() / "app-connect-provider-credentials";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const paths = app_test_paths(root);

  std::istringstream anthropic_input("anthropic-api-key\n");
  std::ostringstream anthropic_out;
  std::ostringstream anthropic_err;
  auto const anthropic_exit = ava::app::run_connect_provider_credential(
      paths,
      ava::app::ConnectProviderCredentialOptions{.provider_id = "anthropic",
                                                 .credential_type = ava::app::ConnectCredentialType::ApiKey,
                                                 .env_var = std::nullopt},
      anthropic_input, anthropic_out, anthropic_err);
  expect(anthropic_exit == 0 && anthropic_err.str().empty() &&
             anthropic_out.str().find("Stored API key credential") != std::string::npos,
         "headless provider connect stores Anthropic API key from stdin");
  ava::tests::FakeTransport transport({});
  auto anthropic = ava::config::provider_credential_for_request(paths, "anthropic", transport);
  expect(anthropic && anthropic->has_value() && (*anthropic)->access_token == "anthropic-api-key" &&
             (*anthropic)->credential_type == "api_key",
         "headless provider connect writes loadable Anthropic API key auth");

  ScopedEnvVar moonshot_key("AVA_TEST_MOONSHOT_KEY", "moonshot-api-key");
  std::istringstream moonshot_input;
  std::ostringstream moonshot_out;
  std::ostringstream moonshot_err;
  auto const moonshot_exit = ava::app::run_connect_provider_credential(
      paths,
      ava::app::ConnectProviderCredentialOptions{.provider_id = "moonshot",
                                                 .credential_type = ava::app::ConnectCredentialType::ApiKey,
                                                 .env_var = "AVA_TEST_MOONSHOT_KEY"},
      moonshot_input, moonshot_out, moonshot_err);
  expect(moonshot_exit == 0 && moonshot_err.str().empty(),
         "headless provider connect stores Moonshot API key from environment");
  auto moonshot = ava::config::provider_credential_for_request(paths, "moonshot", transport);
  expect(moonshot && moonshot->has_value() && (*moonshot)->access_token == "moonshot-api-key" &&
             (*moonshot)->credential_type == "api_key",
         "headless provider connect writes loadable Moonshot API key auth");
  anthropic = ava::config::provider_credential_for_request(paths, "anthropic", transport);
  expect(anthropic && anthropic->has_value() && (*anthropic)->access_token == "anthropic-api-key",
         "headless provider connect preserves existing provider credentials when adding another provider");

  std::istringstream invalid_env_input;
  std::ostringstream invalid_env_out;
  std::ostringstream invalid_env_err;
  auto const invalid_env_exit = ava::app::run_connect_provider_credential(
      paths,
      ava::app::ConnectProviderCredentialOptions{.provider_id = "anthropic",
                                                 .credential_type = ava::app::ConnectCredentialType::ApiKey,
                                                 .env_var = "sk-should-not-be-echoed"},
      invalid_env_input, invalid_env_out, invalid_env_err);
  expect(invalid_env_exit == 1 && invalid_env_out.str().empty() &&
             invalid_env_err.str().find("credential env var name is invalid") != std::string::npos &&
             invalid_env_err.str().find("sk-should-not-be-echoed") == std::string::npos,
         "headless provider connect rejects invalid env names without echoing secrets");

  std::istringstream missing_env_input;
  std::ostringstream missing_env_out;
  std::ostringstream missing_env_err;
  auto const missing_env_exit = ava::app::run_connect_provider_credential(
      paths,
      ava::app::ConnectProviderCredentialOptions{.provider_id = "anthropic",
                                                 .credential_type = ava::app::ConnectCredentialType::ApiKey,
                                                 .env_var = "SKSHOULDNOTBEECHOED"},
      missing_env_input, missing_env_out, missing_env_err);
  expect(missing_env_exit == 1 && missing_env_out.str().empty() &&
             missing_env_err.str().find("credential env var is not set") != std::string::npos &&
             missing_env_err.str().find("SKSHOULDNOTBEECHOED") == std::string::npos,
         "headless provider connect omits env var names from missing-env errors");

  std::istringstream empty_stdin_input("\r\n");
  std::ostringstream empty_stdin_out;
  std::ostringstream empty_stdin_err;
  auto const empty_stdin_exit = ava::app::run_connect_provider_credential(
      paths,
      ava::app::ConnectProviderCredentialOptions{.provider_id = "anthropic",
                                                 .credential_type = ava::app::ConnectCredentialType::ApiKey,
                                                 .env_var = std::nullopt},
      empty_stdin_input, empty_stdin_out, empty_stdin_err);
  expect(empty_stdin_exit == 1 && empty_stdin_out.str().empty() &&
             empty_stdin_err.str().find("credential stdin was empty") != std::string::npos,
         "headless provider connect rejects empty stdin credentials");

  auto const wizard_root = temp_root() / "app-connect-provider-wizard";
  std::error_code wizard_remove_error;
  std::filesystem::remove_all(wizard_root, wizard_remove_error);
  auto const wizard_paths = app_test_paths(wizard_root);
  std::istringstream wizard_input("anthropic\nwizard-api-key\n");
  std::ostringstream wizard_out;
  std::ostringstream wizard_err;
  auto const wizard_exit = ava::app::run_connect_provider_wizard(
      wizard_paths,
      ava::app::ConnectProviderWizardOptions{
          .provider_id = std::nullopt, .credential_type = std::nullopt, .stdin_is_tty = true},
      wizard_input, wizard_out, wizard_err);
  expect(wizard_exit == 0 && wizard_err.str().empty() && wizard_out.str().find("Add credential") != std::string::npos &&
             wizard_out.str().find("Select provider") != std::string::npos &&
             wizard_out.str().find("Stored anthropic API key credential") != std::string::npos,
         "interactive provider wizard opens a searchable provider menu before prompting for secret");
  auto wizard_anthropic = ava::config::provider_credential_for_request(wizard_paths, "anthropic", transport);
  expect(wizard_anthropic && wizard_anthropic->has_value() && (*wizard_anthropic)->access_token == "wizard-api-key",
         "interactive provider wizard stores a loadable credential");

  auto const openai_wizard_root = temp_root() / "app-connect-openai-wizard";
  std::error_code openai_wizard_remove_error;
  std::filesystem::remove_all(openai_wizard_root, openai_wizard_remove_error);
  auto const openai_wizard_paths = app_test_paths(openai_wizard_root);
  std::istringstream openai_wizard_input("3\nwizard-openai-api-key\n");
  std::ostringstream openai_wizard_out;
  std::ostringstream openai_wizard_err;
  auto const openai_wizard_exit = ava::app::run_connect_openai_wizard(
      openai_wizard_paths,
      ava::app::ConnectProviderWizardOptions{
          .provider_id = "openai", .credential_type = std::nullopt, .stdin_is_tty = true},
      openai_wizard_input, openai_wizard_out, openai_wizard_err);
  expect(openai_wizard_exit == 0 && openai_wizard_err.str().empty() &&
             openai_wizard_out.str().find("OpenAI login method") != std::string::npos &&
             openai_wizard_out.str().find("ChatGPT Pro/Plus (headless OAuth)") != std::string::npos &&
             openai_wizard_out.str().find("Stored openai API key credential") != std::string::npos,
         "interactive OpenAI connect command opens method picker and stores selected API key credential");
  auto openai_wizard_credential = ava::config::load_openai_credential(openai_wizard_paths);
  expect(openai_wizard_credential && openai_wizard_credential->has_value() &&
             (*openai_wizard_credential)->type == ava::config::OpenAICredentialType::ApiKey &&
             (*openai_wizard_credential)->access_token == "wizard-openai-api-key",
         "interactive OpenAI connect command writes a loadable OpenAI credential");

  std::istringstream non_tty_openai_wizard_input;
  std::ostringstream non_tty_openai_wizard_out;
  std::ostringstream non_tty_openai_wizard_err;
  auto const non_tty_openai_wizard_exit = ava::app::run_connect_openai_wizard(
      openai_wizard_paths,
      ava::app::ConnectProviderWizardOptions{
          .provider_id = "openai", .credential_type = std::nullopt, .stdin_is_tty = false},
      non_tty_openai_wizard_input, non_tty_openai_wizard_out, non_tty_openai_wizard_err);
  expect(non_tty_openai_wizard_exit == 2 && non_tty_openai_wizard_out.str().empty() &&
             non_tty_openai_wizard_err.str().find("--headless-oauth") != std::string::npos,
         "interactive OpenAI connect command points non-tty callers at headless OAuth");

  std::istringstream cancelled_wizard_input("\x1b");
  std::ostringstream cancelled_wizard_out;
  std::ostringstream cancelled_wizard_err;
  auto const cancelled_wizard_exit = ava::app::run_connect_provider_wizard(
      wizard_paths,
      ava::app::ConnectProviderWizardOptions{
          .provider_id = std::nullopt, .credential_type = std::nullopt, .stdin_is_tty = true},
      cancelled_wizard_input, cancelled_wizard_out, cancelled_wizard_err);
  expect(cancelled_wizard_exit == 1 && cancelled_wizard_err.str().find("provider login cancelled") != std::string::npos,
         "interactive provider wizard cancels on standalone escape without waiting for more input");

  std::istringstream arrow_wizard_input("\x1b[B\narrow-api-key\n");
  std::ostringstream arrow_wizard_out;
  std::ostringstream arrow_wizard_err;
  auto const arrow_wizard_exit = ava::app::run_connect_provider_wizard(
      wizard_paths,
      ava::app::ConnectProviderWizardOptions{
          .provider_id = std::nullopt, .credential_type = std::nullopt, .stdin_is_tty = true},
      arrow_wizard_input, arrow_wizard_out, arrow_wizard_err);
  expect(arrow_wizard_exit == 0 && arrow_wizard_err.str().empty(),
         "interactive provider wizard supports arrow-key provider selection");
  wizard_anthropic = ava::config::provider_credential_for_request(wizard_paths, "anthropic", transport);
  expect(wizard_anthropic && wizard_anthropic->has_value() && (*wizard_anthropic)->access_token == "arrow-api-key",
         "interactive provider wizard arrow selection stores the selected provider credential");

  std::istringstream ignored_escape_wizard_input("\x1b[Canthropic\nright-arrow-api-key\n");
  std::ostringstream ignored_escape_wizard_out;
  std::ostringstream ignored_escape_wizard_err;
  auto const ignored_escape_wizard_exit = ava::app::run_connect_provider_wizard(
      wizard_paths,
      ava::app::ConnectProviderWizardOptions{
          .provider_id = std::nullopt, .credential_type = std::nullopt, .stdin_is_tty = true},
      ignored_escape_wizard_input, ignored_escape_wizard_out, ignored_escape_wizard_err);
  expect(ignored_escape_wizard_exit == 0 && ignored_escape_wizard_err.str().empty(),
         "interactive provider wizard ignores unsupported escape sequences without polluting search text");
  expect(ignored_escape_wizard_out.str().find("Search: anthropic") != std::string::npos,
         "interactive provider wizard keeps typed search text after unsupported escape sequence");
  wizard_anthropic = ava::config::provider_credential_for_request(wizard_paths, "anthropic", transport);
  expect(
      wizard_anthropic && wizard_anthropic->has_value() && (*wizard_anthropic)->access_token == "right-arrow-api-key",
      "interactive provider wizard stores typed provider after ignoring unsupported escape sequence");

  std::istringstream non_tty_wizard_input;
  std::ostringstream non_tty_wizard_out;
  std::ostringstream non_tty_wizard_err;
  auto const non_tty_wizard_exit = ava::app::run_connect_provider_wizard(
      wizard_paths,
      ava::app::ConnectProviderWizardOptions{.provider_id = "anthropic",
                                             .credential_type = ava::app::ConnectCredentialType::ApiKey,
                                             .stdin_is_tty = false},
      non_tty_wizard_input, non_tty_wizard_out, non_tty_wizard_err);
  expect(non_tty_wizard_exit == 2 && non_tty_wizard_out.str().empty() &&
             non_tty_wizard_err.str().find("interactive provider login requires a terminal") != std::string::npos,
         "interactive provider wizard refuses to prompt without a tty");
}

void test_app_print_json_mode_outputs_runtime_events()
{
  auto const root = temp_root() / "app-print-json";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.mode = ava::agent::Mode::Plan;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "print json test opens runtime session");
  if (!session) return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({ava::provider::HttpResponse{
      .status_code = 200,
      .headers = {},
      .body = "data: {\"type\":\"response.output_text.delta\",\"delta\":\"json answer\"}\n\n"
              "data: [DONE]\n\n",
  }});
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.access_token = "token";
  ava::app::PrintModeRunOptions const run_options{.output_format = ava::app::PrintOutputFormat::Json,
                                                  .runtime_options = runtime_options};
  std::ostringstream out;
  std::ostringstream err;
  auto result = ava::app::run_print_prompt(*session, "json prompt", provider, transport, run_options, out, err);
  auto const jsonl = out.str();
  auto const last_break = jsonl.size() > 1 ? jsonl.rfind('\n', jsonl.size() - 2) : std::string::npos;
  auto const last_line = jsonl.substr(last_break == std::string::npos ? 0 : last_break + 1);
  expect(result && result->final_text == "json answer", "print json mode returns agent result");
  expect(err.str().empty(), "print json mode leaves diagnostics on stderr only when needed");
  expect(std::count(jsonl.begin(), jsonl.end(), '\n') == 4 && jsonl.find("\"schema_version\":1") != std::string::npos &&
             jsonl.find("\"event_id\":\"event_") != std::string::npos &&
             jsonl.find("\"name\":\"session_start\"") != std::string::npos &&
             jsonl.find("\"name\":\"user_message\"") != std::string::npos &&
             jsonl.find("\"name\":\"assistant_message\"") != std::string::npos &&
             jsonl.find("\"payload\":{\"text\":\"json answer\"}") != std::string::npos &&
             last_line.find("\"name\":\"done\"") != std::string::npos,
         "print json mode writes JSONL event envelopes ending in done");

  auto error_session = ava::app::open_runtime_session(open_options);
  expect(error_session.has_value(), "print json error test opens runtime session");
  if (!error_session) return;
  ava::tests::FakeTransport error_transport({ava::provider::HttpResponse{
      .status_code = 500,
      .headers = {},
      .body = "upstream failure",
  }});
  std::ostringstream error_out;
  std::ostringstream error_err;
  auto error_result = ava::app::run_print_prompt(*error_session, "json error", provider, error_transport, run_options,
                                                 error_out, error_err);
  auto const error_jsonl = error_out.str();
  auto const error_last_break =
      error_jsonl.size() > 1 ? error_jsonl.rfind('\n', error_jsonl.size() - 2) : std::string::npos;
  auto const error_last_line = error_jsonl.substr(error_last_break == std::string::npos ? 0 : error_last_break + 1);
  expect(!error_result && error_err.str().empty() && error_last_line.find("\"name\":\"error\"") != std::string::npos,
         "print json mode writes failed turns as JSONL envelopes ending in error");
}

void test_app_print_json_mode_streams_provider_deltas_before_final_message()
{
  auto const root = temp_root() / "app-print-json-streaming";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);

  ava::app::RuntimeOpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.mode = ava::agent::Mode::Plan;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "print json streaming test opens runtime session");
  if (!session) return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ChunkedStreamingTransport transport({"data: {\"type\":\"response.output_text.delta\",\"delta\":\"json \"}\n\n",
                                       "data: {\"type\":\"response.output_text.delta\",\"delta\":\"stream\"}\n\n",
                                       "data: [DONE]\n\n"});
  ava::app::RuntimeRunOptions runtime_options;
  runtime_options.access_token = "token";
  ava::app::PrintModeRunOptions const run_options{.output_format = ava::app::PrintOutputFormat::Json,
                                                  .runtime_options = runtime_options};
  std::ostringstream out;
  std::ostringstream err;
  auto result =
      ava::app::run_print_prompt(*session, "json streaming prompt", provider, transport, run_options, out, err);
  auto const jsonl = out.str();
  auto const update_position = jsonl.find("\"name\":\"message_update\"");
  auto const final_position = jsonl.find("\"name\":\"assistant_message\"");
  expect(result && result->final_text == "json stream", "print json streaming mode returns accumulated final text");
  expect(update_position != std::string::npos && final_position != std::string::npos &&
             update_position < final_position && jsonl.find("\"name\":\"message_end\"") != std::string::npos,
         "print json mode emits streaming message deltas before final assistant message");
}

}  // namespace

void run_app_print_tests()
{
  test_app_print_prompt_merging();
  test_headless_permission_policy();
  test_app_print_text_mode_outputs_final_text_only();
  test_app_print_text_mode_with_streaming_keeps_stdout_final_only();
  test_app_print_text_mode_reports_stdout_write_failure();
  test_app_print_mode_uses_headless_permission_policy();
  test_app_print_mode_uses_persistent_permission_rules();
  test_app_print_mode_refreshes_expired_oauth_before_provider_request();
  test_app_connect_provider_credentials_headlessly();
  test_app_print_json_mode_outputs_runtime_events();
  test_app_print_json_mode_streams_provider_deltas_before_final_message();
}
