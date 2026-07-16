#include "sys.h"
#include "tests/support/app_runtime_support.h"
#include "tests/support/fake_transport.h"
#include "tests/support/test_harness.h"
#include "ava/observability/run_observer.h"
#include "ava/app/commands.h"
#include "ava/app/events.h"
#include "ava/app/runtime.h"
#include "ava/session/compaction.h"
#include "ava/session/export.h"
#include "ava/session/session_store.h"
#include "ava/session/stats.h"
#include "ava/provider/openai_provider.h"
#include "ava/provider/provider_utils.h"
#include "ava/core/json.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace {

using namespace ava::tests;

class ThrowingRunObserver final : public ava::observability::RunObserver
{
 public:
  void on_event(ava::observability::TraceEvent const&) override { throw std::runtime_error("observer failure"); }
};

class CancelAfterRequestTransport final : public ava::provider::Transport
{
 public:
  explicit CancelAfterRequestTransport(ava::provider::HttpResponse response) : response_(std::move(response)) { }

  [[nodiscard]] ava::core::Result<ava::provider::HttpResponse> send(ava::provider::HttpRequest const& request) override
  {
    requests_.push_back(request);
    canceled_ = true;
    return response_;
  }

  [[nodiscard]] bool canceled() const noexcept { return canceled_; }
  [[nodiscard]] std::vector<ava::provider::HttpRequest> const& requests() const noexcept { return requests_; }

 private:
  ava::provider::HttpResponse response_;
  bool canceled_ = false;
  std::vector<ava::provider::HttpRequest> requests_;
};

void test_app_compact_provider_summary_success()
{
  auto const root = temp_root() / "app-compact-provider-success";
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
  expect(session.has_value(), "provider-backed /compact test opens runtime session");
  if (!session)
    return;
  auto seeded = session->store.append(ava::session::SessionEntry{.id = "entry_user_compact_source",
                                                                 .parent_id = "",
                                                                 .type = ava::session::EntryType::UserMessage,
                                                                 .timestamp = "2026-05-01T00:00:00Z",
                                                                 .data_json = "{\"text\":\"Goal: refactor compaction\"}"});
  expect(seeded.has_value(), "provider-backed /compact test seeds source entry");
  auto seeded_reasoning = session->store.append(ava::session::SessionEntry{
      .id = "entry_reasoning_compact_source",
      .parent_id = "",
      .type = ava::session::EntryType::ReasoningBlock,
      .timestamp = "2026-05-01T00:00:01Z",
      .data_json =
          R"({"provider":"anthropic","model":"claude","format":"anthropic_thinking","text":"visible compact reasoning","signature":"compact-secret-signature","redacted_data":"opaque-compaction-redacted","redacted":false})"});
  expect(seeded_reasoning.has_value(), "provider-backed /compact test seeds reasoning source entry");
  auto seeded_redacted_reasoning = session->store.append(ava::session::SessionEntry{
      .id = "entry_redacted_reasoning_compact_source",
      .parent_id = "",
      .type = ava::session::EntryType::ReasoningBlock,
      .timestamp = "2026-05-01T00:00:02Z",
      .data_json =
          R"({"provider":"anthropic","model":"claude","format":"anthropic_thinking","text":"hidden redacted compact reasoning","signature":"redacted-compact-secret","redacted_data":"opaque-hidden-compaction-redacted","redacted": true })"});
  expect(seeded_redacted_reasoning.has_value(), "provider-backed /compact test seeds redacted reasoning source entry");

  std::string const summary =
      "# Goal\nShip compact\n# Constraints / Preferences\nKeep provider backed\n# Decisions\nUse callback\n"
      "# Files Read or Modified\nsrc/ava/app/commands.cpp\n# Unresolved Tasks\nNone noted.\n# Next Steps\nRun tests.";
  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport(
      {ava::provider::HttpResponse{.status_code = 200, .headers = {}, .body = "{\"output_text\":\"" + ava::core::json::escape(summary) + "\"}"}});
  ava::app::runtime::RunOptions run_options;
  run_options.access_token = "token";

  auto compact = ava::app::run_command(
      *session, ava::app::CommandRequest{
                    .command = "/compact Keep decisions",
                    .compaction_summary_generator = [&](std::vector<ava::session::SessionEntry> const& entries, ava::session::CompactionConfig const& config,
                                                        std::string_view instructions, std::size_t estimated_tokens) {
                      return ava::app::generate_compaction_summary(*session, entries, config, instructions, estimated_tokens, provider, transport, run_options);
                    }});
  expect(compact && compact->handled && !compact->output.empty() && compact->output[0].find("compaction summary recorded") != std::string::npos,
         "/compact records a provider-generated summary");
  expect(transport.requests().size() == 1 && transport.requests()[0].body.find("Goal: refactor compaction") != std::string::npos &&
             transport.requests()[0].body.find("visible compact reasoning") != std::string::npos &&
             transport.requests()[0].body.find("hidden redacted compact reasoning") == std::string::npos &&
             transport.requests()[0].body.find("signature_present") != std::string::npos &&
             transport.requests()[0].body.find("compact-secret-signature") == std::string::npos &&
             transport.requests()[0].body.find("redacted-compact-secret") == std::string::npos &&
             transport.requests()[0].body.find("opaque-compaction-redacted") == std::string::npos &&
             transport.requests()[0].body.find("opaque-hidden-compaction-redacted") == std::string::npos &&
             transport.requests()[0].body.find("# Files Read or Modified") != std::string::npos &&
             transport.requests()[0].body.find("Keep decisions") != std::string::npos,
         "provider-backed /compact sends deterministic prompt with sanitized source data and required sections");

  auto entries = session->store.load();
  expect(entries && std::ranges::any_of(*entries,
                                        [&](ava::session::SessionEntry const& entry) {
                                          return entry.type == ava::session::EntryType::Compaction &&
                                                 ava::core::json::string_field(entry.data_json, "summary") == summary &&
                                                 entry.data_json.find("\"summary_unavailable\":false") != std::string::npos;
                                        }),
         "/compact appends returned summary with summary_unavailable false");
}

void test_app_compact_openai_oauth_streaming_summary_success()
{
  auto const root = temp_root() / "app-compact-oauth-streaming";
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
  expect(session.has_value(), "OAuth streaming /compact test opens runtime session");
  if (!session)
    return;

  std::string const summary = "# Goal\nLive compaction works.";
  std::string const sse_body = "data: {\"type\":\"response.output_text.delta\",\"delta\":\"" + ava::core::json::escape(summary) +
                               "\"}\n\n"
                               "data: [DONE]\n\n";
  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({ava::provider::HttpResponse{.status_code = 200, .headers = {}, .body = sse_body}});
  ava::app::runtime::RunOptions run_options;
  run_options.access_token = "token";
  run_options.openai_oauth = true;
  run_options.openai_account_id = "acct_test";

  auto config = ava::session::default_compaction_config();
  auto entries = session->store.load();
  expect(entries.has_value(), "OAuth streaming /compact test loads entries");
  if (!entries)
    return;
  auto generated = ava::app::generate_compaction_summary(*session, *entries, config, "live", 12, provider, transport, run_options);
  expect(generated && *generated == summary, "OAuth streaming compaction summary parses SSE text deltas");
  expect(transport.requests().size() == 1 && transport.requests()[0].url == "https://chatgpt.com/backend-api/codex/responses" &&
             transport.requests()[0].body.find("\"stream\":true") != std::string::npos &&
             transport.requests()[0].body.find("\"store\":false") != std::string::npos,
         "OAuth compaction summary request uses delegated streaming request shape");
}

void test_app_compact_provider_failure_leaves_session_untouched()
{
  auto const root = temp_root() / "app-compact-provider-failure";
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
  expect(session.has_value(), "provider failure /compact test opens runtime session");
  if (!session)
    return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({ava::provider::HttpResponse{.status_code = 500, .headers = {}, .body = "{\"error\":{\"message\":\"boom\"}}"}});
  ava::app::runtime::RunOptions run_options;
  run_options.access_token = "token";

  auto compact = ava::app::run_command(
      *session, ava::app::CommandRequest{
                    .command = "/compact",
                    .compaction_summary_generator = [&](std::vector<ava::session::SessionEntry> const& entries, ava::session::CompactionConfig const& config,
                                                        std::string_view instructions, std::size_t estimated_tokens) {
                      return ava::app::generate_compaction_summary(*session, entries, config, instructions, estimated_tokens, provider, transport, run_options);
                    }});
  auto entries = session->store.load();
  expect(compact && compact->handled && !compact->output.empty() &&
             compact->output[0].find("compaction summary request failed with status 500") != std::string::npos &&
             compact->output[0].find("boom") != std::string::npos,
         "provider-backed /compact reports provider failure with status and body details");
  expect(entries && std::ranges::none_of(*entries, [](ava::session::SessionEntry const& entry) { return entry.type == ava::session::EntryType::Compaction; }),
         "provider-backed /compact failure leaves session without compaction entry");
}

void test_compaction_observation_preserves_cancellation_callback_contract()
{
  auto const root = temp_root() / "app-compaction-observer-callback";
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  ava::app::runtime::OpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "compaction callback-contract test opens runtime session");
  if (!session)
    return;
  ava::provider::OpenAIProvider const provider("https://api.example.test");
  auto const config = ava::session::default_compaction_config();
  auto const entries = session->store.load();
  expect(entries.has_value(), "compaction callback-contract test loads session entries");
  if (!entries)
    return;

  auto run_summary = [&](std::shared_ptr<ava::observability::RunObservation> observation, bool throwing_callback) {
    ava::tests::FakeTransport transport({ava::provider::HttpResponse{.status_code = 200, .headers = {}, .body = "{\"output_text\":\"summary\"}"}});
    unsigned callback_calls = 0;
    ava::app::runtime::RunOptions options;
    options.access_token = "token";
    options.observation = std::move(observation);
    options.cancel_requested = [&callback_calls, throwing_callback]() -> bool {
      ++callback_calls;
      if (throwing_callback)
        throw std::runtime_error("authoritative callback failure");
      return false;
    };
    bool callback_threw = false;
    ava::core::Result<std::string> result = std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "not run"));
    try
    {
      result = ava::app::generate_compaction_summary(*session, *entries, config, "", 1, provider, transport, options);
    }
    catch (std::runtime_error const&)
    {
      callback_threw = true;
    }
    return std::tuple{std::move(result), callback_calls, callback_threw};
  };

  auto const [disabled_result, disabled_calls, disabled_threw] = run_summary(nullptr, false);
  auto throwing_observer = std::make_shared<ThrowingRunObserver>();
  auto enabled_observation = std::make_shared<ava::observability::RunObservation>(throwing_observer);
  auto const [enabled_result, enabled_calls, enabled_threw] = run_summary(enabled_observation, false);
  auto const [disabled_throw_result, disabled_throw_calls, disabled_throw_threw] = run_summary(nullptr, true);
  auto const [enabled_throw_result, enabled_throw_calls, enabled_throw_threw] = run_summary(enabled_observation, true);
  expect(disabled_result && enabled_result && *disabled_result == "summary" && *enabled_result == "summary" && disabled_calls == 3 && enabled_calls == 3 &&
             !disabled_threw && !enabled_threw && enabled_observation->counters().callback_failures == 1 && !disabled_throw_result && !enabled_throw_result &&
             disabled_throw_threw && enabled_throw_threw && disabled_throw_calls == 1 && enabled_throw_calls == 1,
         "compaction observation isolates observer failures and preserves stateful and throwing cancellation callback behavior/counts");
}

void test_app_auto_compaction_provider_cancellation_leaves_session_untouched()
{
  auto const root = temp_root() / "app-auto-compact-canceled";
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
  expect(session.has_value(), "provider cancellation auto compaction test opens runtime session");
  if (!session)
    return;
  session->model.context_window_tokens = 100;
  static_cast<void>(session->store.append(ava::session::SessionEntry{.id = "entry_canceled_auto_compact",
                                                                     .parent_id = "",
                                                                     .type = ava::session::EntryType::UserMessage,
                                                                     .timestamp = ava::session::now_timestamp(),
                                                                     .data_json = "{\"text\":\"" + std::string(420, 'c') + "\"}"}));

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  CancelAfterRequestTransport transport(ava::provider::HttpResponse{.status_code = 200, .headers = {}, .body = "{\"output_text\":\"CANCELED SUMMARY\"}"});
  ava::app::runtime::RunOptions run_options;
  run_options.access_token = "token";
  run_options.cancel_requested = [&transport] { return transport.canceled(); };

  auto result = ava::app::run_prompt(*session, "cancel during compaction", provider, transport, run_options);
  auto entries = session->store.load();
  expect(!result && result.error().message() == "agent loop canceled", "auto compaction reports cancellation raised during the provider summary request");
  expect(transport.requests().size() == 1, "canceled auto compaction dispatches only the summary request");
  expect(entries && count_compaction_entries(*entries) == 0, "canceled auto compaction leaves no partial compaction entry");
}

void test_app_compact_oversized_summary_leaves_session_untouched()
{
  auto const root = temp_root() / "app-compact-oversized";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  std::filesystem::create_directories(paths.ava_config_dir);
  {
    std::ofstream file(paths.compaction_file, std::ios::binary | std::ios::trunc);
    file << "{\"max_summary_bytes\":8}";
  }

  ava::app::runtime::OpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "oversized /compact test opens runtime session");
  if (!session)
    return;

  auto compact = ava::app::run_command(
      *session, ava::app::CommandRequest{
                    .command = "/compact",
                    .compaction_summary_generator = [](std::vector<ava::session::SessionEntry> const&, ava::session::CompactionConfig const&, std::string_view,
                                                       std::size_t) -> ava::core::Result<std::string> { return std::string("this summary is too large"); }});
  auto entries = session->store.load();
  expect(compact && compact->handled && !compact->output.empty() && compact->output[0].find("generated compaction summary is too large") != std::string::npos,
         "/compact reports oversized generated summary");
  expect(entries && std::ranges::none_of(*entries, [](ava::session::SessionEntry const& entry) { return entry.type == ava::session::EntryType::Compaction; }),
         "oversized generated summary leaves session without compaction entry");
}

void test_app_compact_cancellation_before_append_leaves_session_untouched()
{
  auto const root = temp_root() / "app-compact-cancel-before-append";
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
  expect(session.has_value(), "manual compaction cancellation test opens runtime session");
  if (!session)
    return;

  bool cancel = false;
  auto compact = ava::app::run_command(
      *session,
      ava::app::CommandRequest{.command = "/compact",
                               .compaction_summary_generator = [&cancel](std::vector<ava::session::SessionEntry> const&, ava::session::CompactionConfig const&,
                                                                         std::string_view, std::size_t) -> ava::core::Result<std::string> {
                                 cancel = true;
                                 return std::string("summary generated just before cancellation");
                               },
                               .cancel_requested = [&cancel] { return cancel; },
                               .propagate_compaction_errors = true});
  auto entries = session->store.load();
  expect(!compact && compact.error().message() == "agent loop canceled", "manual compaction observes cancellation before appending the generated summary");
  expect(entries && count_compaction_entries(*entries) == 0, "manual compaction cancellation leaves no partial compaction entry");
}

void test_app_compaction_prompt_builder_sections()
{
  auto config = ava::session::default_compaction_config();
  std::vector<ava::session::SessionEntry> const entries = {
      ava::session::SessionEntry{.id = "entry_tool",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::ToolResult,
                                 .timestamp = "2026-05-01T00:00:00Z",
                                 .data_json = "{\"name\":\"read\",\"result\":\"src/main.cpp contents\"}"},
      ava::session::SessionEntry{.id = "entry_replay",
                                 .parent_id = "",
                                 .type = ava::session::EntryType::UserMessage,
                                 .timestamp = "2026-05-01T00:00:01Z",
                                 .data_json = "{\"text\":\"duplicated active prompt\",\"internal_replay\":true,"
                                              "\"replay_of\":\"entry_user\"}"}};
  auto const prompt = ava::app::build_compaction_summary_prompt(entries, config, "preserve files", 42);
  expect(prompt.find("# Goal") != std::string::npos && prompt.find("# Constraints / Preferences") != std::string::npos &&
             prompt.find("# Files Read or Modified") != std::string::npos && prompt.find("src/main.cpp") != std::string::npos &&
             prompt.find("preserve files") != std::string::npos && prompt.find("internal_replay") == std::string::npos &&
             prompt.find("duplicated active prompt") == std::string::npos,
         "compaction prompt builder includes source data and skips internal replay messages");
}

void test_app_auto_compaction_appends_summary_and_rebuilds_context()
{
  auto const root = temp_root() / "app-auto-compact";
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
  expect(session.has_value(), "auto compaction test opens runtime session");
  if (!session)
    return;
  session->model.context_window_tokens = 100;

  std::string const old_context = "old context marker " + std::string(420, 'x');
  static_cast<void>(session->store.append(ava::session::SessionEntry{.id = "entry_old_user",
                                                                     .parent_id = "",
                                                                     .type = ava::session::EntryType::UserMessage,
                                                                     .timestamp = ava::session::now_timestamp(),
                                                                     .data_json = "{\"text\":\"" + ava::core::json::escape(old_context) + "\"}"}));
  for (int index = 0; index < 6; ++index)
  {
    static_cast<void>(session->store.append(ava::session::SessionEntry{.id = "entry_recent_" + std::to_string(index),
                                                                       .parent_id = "",
                                                                       .type = ava::session::EntryType::AssistantMessage,
                                                                       .timestamp = ava::session::now_timestamp(),
                                                                       .data_json = "{\"text\":\"recent filler " + std::to_string(index) + "\"}"}));
  }

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({ava::provider::HttpResponse{.status_code = 200, .headers = {}, .body = "{\"output_text\":\"AUTO SUMMARY\"}"},
                                       sse_response(final_text_sse("compacted answer"))});
  ava::app::runtime::RunOptions run_options;
  run_options.access_token = "token";

  auto result = ava::app::run_prompt(*session, "continue after compaction", provider, transport, run_options);
  auto entries = session->store.load();
  auto const compaction = entries ? latest_compaction_entry(*entries) : std::nullopt;
  expect(result && result->final_text == "compacted answer", "auto compaction prompt succeeds");
  expect(transport.requests().size() == 2, "auto compaction performs summary request then provider request");
  expect(transport.requests().size() == 2 && transport.requests()[0].body.find("old context marker") != std::string::npos,
         "auto compaction summary request sees pre-compaction context");
  expect(transport.requests().size() == 2 && transport.requests()[1].body.find("AUTO SUMMARY") != std::string::npos &&
             transport.requests()[1].body.find("\"content\":\"continue after compaction\"") != std::string::npos &&
             transport.requests()[1].body.find("old context marker") == std::string::npos,
         "provider request is rebuilt from the new compaction boundary with the active prompt as a normal user turn");
  expect(compaction && compaction->data_json.find("\"trigger\":\"auto\"") != std::string::npos &&
             compaction->data_json.find("\"summary\":\"AUTO SUMMARY\"") != std::string::npos &&
             compaction->data_json.find("\"threshold_tokens\":80") != std::string::npos &&
             compaction->data_json.find("\"keep_recent_messages\":6") != std::string::npos &&
             compaction->data_json.find("\"model\":\"gpt-5.5\"") != std::string::npos,
         "auto compaction entry records trigger, summary, threshold, retention, and model metadata");
}

void test_app_auto_compaction_recent_context_respects_token_budget()
{
  auto const root = temp_root() / "app-auto-compact-recent-budget";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  std::filesystem::create_directories(paths.ava_config_dir);
  {
    std::ofstream file(paths.compaction_file, std::ios::binary | std::ios::trunc);
    file << "{\"auto_threshold_tokens\":1,\"keep_recent_tokens\":20,\"keep_recent_messages\":8}";
  }

  ava::app::runtime::OpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "recent context token budget test opens runtime session");
  if (!session)
    return;
  session->model.context_window_tokens = 1000;
  for (int index = 0; index < 4; ++index)
  {
    static_cast<void>(session->store.append(
        ava::session::SessionEntry{.id = "entry_budget_" + std::to_string(index),
                                   .parent_id = "",
                                   .type = ava::session::EntryType::UserMessage,
                                   .timestamp = ava::session::now_timestamp(),
                                   .data_json = "{\"text\":\"budget filler " + std::to_string(index) + " " + std::string(160, 'b') + "\"}"}));
  }

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({ava::provider::HttpResponse{.status_code = 200, .headers = {}, .body = "{\"output_text\":\"BUDGET SUMMARY\"}"},
                                       sse_response(final_text_sse("budget answer"))});
  ava::app::runtime::RunOptions run_options;
  run_options.access_token = "token";

  auto result = ava::app::run_prompt(*session, "after budget compaction", provider, transport, run_options);
  auto entries = session->store.load();
  auto const compaction = entries ? latest_compaction_entry(*entries) : std::nullopt;
  auto const recent_context = compaction ? ava::core::json::string_field(compaction->data_json, "recent_context") : std::optional<std::string>{};
  expect(result && result->final_text == "budget answer", "recent context token budget prompt succeeds");
  expect(recent_context && recent_context->find("recent context tail truncated") != std::string::npos && ava::session::estimate_tokens(*recent_context) <= 20,
         "auto compaction stores recent context bounded by keep_recent_tokens with an explicit marker");
}

void test_app_auto_compaction_recent_context_truncates_utf8_safely()
{
  auto const root = temp_root() / "app-auto-compact-recent-utf8";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  std::filesystem::create_directories(paths.ava_config_dir);
  {
    std::ofstream file(paths.compaction_file, std::ios::binary | std::ios::trunc);
    file << "{\"auto_threshold_tokens\":1,\"keep_recent_tokens\":20,\"keep_recent_messages\":8}";
  }

  ava::app::runtime::OpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "recent context UTF-8 truncation test opens runtime session");
  if (!session)
    return;
  session->model.context_window_tokens = 1000;

  std::string emoji_tail;
  for (int index = 0; index < 80; ++index) emoji_tail += "\xF0\x9F\x98\x80";
  static_cast<void>(session->store.append(ava::session::SessionEntry{.id = "entry_utf8_budget",
                                                                     .parent_id = "",
                                                                     .type = ava::session::EntryType::UserMessage,
                                                                     .timestamp = ava::session::now_timestamp(),
                                                                     .data_json = "{\"text\":\"" + ava::core::json::escape(emoji_tail) + "\"}"}));

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({ava::provider::HttpResponse{.status_code = 200, .headers = {}, .body = "{\"output_text\":\"UTF8 SUMMARY\"}"},
                                       sse_response(final_text_sse("utf8 answer"))});
  ava::app::runtime::RunOptions run_options;
  run_options.access_token = "token";

  auto result = ava::app::run_prompt(*session, "after utf8 compaction", provider, transport, run_options);
  auto entries = session->store.load();
  auto const compaction = entries ? latest_compaction_entry(*entries) : std::nullopt;
  auto const recent_context = compaction ? ava::core::json::string_field(compaction->data_json, "recent_context") : std::optional<std::string>{};
  expect(result && result->final_text == "utf8 answer", "recent context UTF-8 prompt succeeds");
  auto const marker_end = recent_context ? recent_context->find('\n') : std::string::npos;
  bool const suffix_starts_on_codepoint = recent_context && marker_end != std::string::npos && marker_end + 1 < recent_context->size() &&
                                          (static_cast<unsigned char>((*recent_context)[marker_end + 1]) & 0xC0U) != 0x80U;
  expect(recent_context && recent_context->find("recent context tail truncated") != std::string::npos && suffix_starts_on_codepoint,
         "recent context truncation starts UTF-8 suffix on a code point boundary");
}

void test_app_auto_compaction_explicit_zero_disables()
{
  auto const root = temp_root() / "app-auto-compact-disabled";
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  auto const workspace = root / "workspace";
  auto const paths = app_test_paths(root);
  std::filesystem::create_directories(workspace);
  std::filesystem::create_directories(paths.ava_config_dir);
  {
    std::ofstream file(paths.compaction_file, std::ios::binary | std::ios::trunc);
    file << "{\"auto_threshold_tokens\":0}";
  }

  ava::app::runtime::OpenOptions open_options;
  open_options.workspace_dir = workspace;
  open_options.current_dir = workspace;
  open_options.paths = paths;
  auto session = ava::app::open_runtime_session(open_options);
  expect(session.has_value(), "disabled auto compaction test opens runtime session");
  if (!session)
    return;
  session->model.context_window_tokens = 10;
  static_cast<void>(session->store.append(ava::session::SessionEntry{.id = "entry_big_user",
                                                                     .parent_id = "",
                                                                     .type = ava::session::EntryType::UserMessage,
                                                                     .timestamp = ava::session::now_timestamp(),
                                                                     .data_json = "{\"text\":\"" + std::string(240, 'd') + "\"}"}));

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({sse_response(final_text_sse("no compact answer"))});
  ava::app::runtime::RunOptions run_options;
  run_options.access_token = "token";

  auto result = ava::app::run_prompt(*session, "do not compact", provider, transport, run_options);
  auto entries = session->store.load();
  expect(result && result->final_text == "no compact answer", "explicit disabled auto compaction prompt succeeds");
  expect(transport.requests().size() == 1, "explicit disabled auto compaction does not call summary provider");
  expect(entries && count_compaction_entries(*entries) == 0, "explicit disabled auto compaction appends no compaction");
}

void test_app_auto_compaction_uses_default_threshold_without_context_window_metadata()
{
  auto const root = temp_root() / "app-auto-compact-default-threshold";
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
  expect(session.has_value(), "default threshold auto compaction test opens runtime session");
  if (!session)
    return;
  session->model.context_window_tokens = std::nullopt;

  auto const config = ava::session::default_compaction_config();
  auto const threshold = ava::session::effective_auto_threshold_tokens(config, std::nullopt);
  static_cast<void>(session->store.append(ava::session::SessionEntry{.id = "entry_default_threshold_big",
                                                                     .parent_id = "",
                                                                     .type = ava::session::EntryType::UserMessage,
                                                                     .timestamp = ava::session::now_timestamp(),
                                                                     .data_json = "{\"text\":\"" + std::string(threshold * 4, 'f') + "\"}"}));

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({ava::provider::HttpResponse{.status_code = 200, .headers = {}, .body = "{\"output_text\":\"DEFAULT SUMMARY\"}"},
                                       sse_response(final_text_sse("default compact answer"))});
  ava::app::runtime::RunOptions run_options;
  run_options.access_token = "token";

  auto result = ava::app::run_prompt(*session, "default threshold prompt", provider, transport, run_options);
  auto entries = session->store.load();
  expect(result && result->final_text == "default compact answer", "default threshold auto compaction prompt succeeds");
  expect(transport.requests().size() == 2, "default threshold auto compaction performs a summary request before provider request");
  expect(entries && count_compaction_entries(*entries) == 1, "default threshold auto compaction appends a compaction entry without model context metadata");
  expect(transport.requests().size() == 2 && transport.requests()[1].body.find("\"content\":\"default threshold prompt\"") != std::string::npos,
         "default threshold auto compaction keeps the active prompt as a normal user message");
}

void test_app_auto_compaction_retries_stale_snapshot_before_append()
{
  auto const root = temp_root() / "app-auto-compact-revalidate";
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
  expect(session.has_value(), "auto compaction revalidation test opens runtime session");
  if (!session)
    return;
  session->model.context_window_tokens = 100;

  static_cast<void>(session->store.append(ava::session::SessionEntry{.id = "entry_revalidate_big",
                                                                     .parent_id = "",
                                                                     .type = ava::session::EntryType::UserMessage,
                                                                     .timestamp = ava::session::now_timestamp(),
                                                                     .data_json = "{\"text\":\"" + std::string(420, 'r') + "\"}"}));

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  MutatingSummaryTransport transport(session->store,
                                     {ava::provider::HttpResponse{.status_code = 200, .headers = {}, .body = "{\"output_text\":\"STALE SUMMARY\"}"},
                                      ava::provider::HttpResponse{.status_code = 200, .headers = {}, .body = "{\"output_text\":\"RETRIED SUMMARY\"}"},
                                      sse_response(final_text_sse("retry after stale"))});
  ava::app::runtime::RunOptions run_options;
  run_options.access_token = "token";

  auto result = ava::app::run_prompt(*session, "retry stale summary", provider, transport, run_options);
  auto entries = session->store.load();
  expect(result && result->final_text == "retry after stale", "auto compaction retries a stale snapshot and continues after a fresh summary");
  expect(transport.requests().size() == 3, "stale auto compaction regenerates one summary before the provider request");
  expect(entries && count_compaction_entries(*entries) == 1, "stale auto compaction appends only the summary generated from the fresh snapshot");
  auto const compaction = entries ? latest_compaction_entry(*entries) : std::nullopt;
  expect(compaction && compaction->data_json.find("RETRIED SUMMARY") != std::string::npos && compaction->data_json.find("STALE SUMMARY") == std::string::npos,
         "auto compaction discards the stale summary instead of recording it");
  expect(entries && std::ranges::any_of(*entries,
                                        [](ava::session::SessionEntry const& entry) { return entry.data_json.find("concurrent change") != std::string::npos; }),
         "auto compaction retry test introduced a concurrent session change");
}

void test_app_auto_compaction_repeated_stale_snapshot_fails_without_append()
{
  auto const root = temp_root() / "app-auto-compact-repeated-stale";
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
  expect(session.has_value(), "repeated stale auto compaction test opens runtime session");
  if (!session)
    return;
  session->model.context_window_tokens = 100;

  static_cast<void>(session->store.append(ava::session::SessionEntry{.id = "entry_repeated_stale_big",
                                                                     .parent_id = "",
                                                                     .type = ava::session::EntryType::UserMessage,
                                                                     .timestamp = ava::session::now_timestamp(),
                                                                     .data_json = "{\"text\":\"" + std::string(420, 's') + "\"}"}));

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  MutatingSummaryTransport transport(session->store,
                                     {ava::provider::HttpResponse{.status_code = 200, .headers = {}, .body = "{\"output_text\":\"STALE ONE\"}"},
                                      ava::provider::HttpResponse{.status_code = 200, .headers = {}, .body = "{\"output_text\":\"STALE TWO\"}"}},
                                     2);
  ava::app::runtime::RunOptions run_options;
  run_options.access_token = "token";

  auto result = ava::app::run_prompt(*session, "repeated stale", provider, transport, run_options);
  auto entries = session->store.load();
  expect(!result && result.error().message().find("session changed during context compaction") != std::string::npos,
         "auto compaction returns a clear stale snapshot error after bounded retries are exhausted");
  expect(transport.requests().size() == 2, "repeated stale auto compaction stops after two summary attempts");
  expect(entries && count_compaction_entries(*entries) == 0, "repeated stale auto compaction appends no summary from stale snapshots");
}

void test_app_context_overflow_compacts_and_retries_once_successfully()
{
  auto const root = temp_root() / "app-context-overflow-retry";
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
  expect(session.has_value(), "context overflow retry test opens runtime session");
  if (!session)
    return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport(
      {ava::provider::HttpResponse{.status_code = 400, .headers = {}, .body = "{\"error\":{\"message\":\"context length exceeded the token limit\"}}"},
       ava::provider::HttpResponse{.status_code = 200, .headers = {}, .body = "{\"output_text\":\"OVERFLOW SUMMARY\"}"},
       sse_response(final_text_sse("retry answer"))});
  ava::app::runtime::RunOptions run_options;
  run_options.access_token = "token";
  std::vector<ava::app::runtime::Event> events;
  run_options.event_sink = [&events](ava::app::runtime::Event const& event) {
    events.push_back(event);
    return ava::core::VoidResult{};
  };

  auto result = ava::app::run_prompt(*session, "overflow prompt", provider, transport, run_options);
  auto entries = session->store.load();
  auto const compaction = entries ? latest_compaction_entry(*entries) : std::nullopt;
  expect(result && result->final_text == "retry answer", "context overflow retry succeeds after compaction");
  expect(transport.requests().size() == 3, "context overflow performs original call, compaction, and one retry");
  expect(compaction && compaction->data_json.find("\"trigger\":\"context_overflow\"") != std::string::npos &&
             compaction->data_json.find("OVERFLOW SUMMARY") != std::string::npos,
         "context overflow retry appends a context_overflow compaction summary");
  expect(std::ranges::any_of(events,
                             [](ava::app::runtime::Event const& event) {
                               return event.type == ava::app::runtime::EventType::Retry && event.reason == "context_overflow" && event.attempt == 1 &&
                                      event.max_attempts == 1;
                             }) &&
             std::ranges::any_of(events,
                                 [](ava::app::runtime::Event const& event) {
                                   return event.type == ava::app::runtime::EventType::CompactionStart && event.trigger == "context_overflow" &&
                                          event.attempt == 1 && event.max_attempts == 2;
                                 }) &&
             std::ranges::any_of(events,
                                 [](ava::app::runtime::Event const& event) {
                                   return event.type == ava::app::runtime::EventType::CompactionEnd &&
                                          event.summary_bytes == std::string("OVERFLOW SUMMARY").size() && event.attempt == 1 && event.max_attempts == 2;
                                 }),
         "context overflow retry emits backend lifecycle events for replaying TUI compaction and retry state");
  expect(transport.requests().size() == 3 && transport.requests()[2].body.find("OVERFLOW SUMMARY") != std::string::npos &&
             transport.requests()[2].body.find("\"content\":\"overflow prompt\"") != std::string::npos &&
             count_substrings(transport.requests()[2].body, "overflow prompt") == 1,
         "context overflow retry rebuilds provider context with one active prompt replay");
  auto const recent_context = compaction ? ava::core::json::string_field(compaction->data_json, "recent_context") : std::optional<std::string>{};
  expect(recent_context && recent_context->find("overflow prompt") == std::string::npos,
         "context overflow compaction excludes active prompts that will be replayed from recent context");
  expect(entries && std::ranges::count_if(*entries, ava::session::is_internal_replay_user_message) == 1,
         "context overflow compaction stores active prompt replay as an internal user message");
  auto const markdown = entries ? ava::session::format_session_markdown(*entries) : std::string{};
  auto const stats = entries ? ava::session::compute_session_stats(*entries) : ava::session::SessionStats{};
  expect(markdown.find("internal_replay") == std::string::npos && count_substrings(markdown, "overflow prompt") == 1 && stats.counts.user_message == 1,
         "consumer-facing export and stats hide internal active prompt replays");
}

void test_app_context_overflow_compaction_failure_leaves_no_partial_entry()
{
  auto const root = temp_root() / "app-context-overflow-compaction-fails";
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
  expect(session.has_value(), "context overflow compaction failure test opens runtime session");
  if (!session)
    return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport(
      {ava::provider::HttpResponse{.status_code = 400, .headers = {}, .body = "{\"error\":{\"message\":\"too many tokens for context window\"}}"},
       ava::provider::HttpResponse{.status_code = 429, .headers = {}, .body = "{\"error\":{\"message\":\"summary quota exhausted\"}}"}});
  ava::app::runtime::RunOptions run_options;
  run_options.access_token = "token";

  auto result = ava::app::run_prompt(*session, "overflow then summary fails", provider, transport, run_options);
  auto entries = session->store.load();
  expect(!result && result.error().message() == "context overflow compaction failed", "context overflow returns clear compaction failure");
  expect(!result && result.error().format().find("429") != std::string::npos && result.error().format().find("summary quota exhausted") != std::string::npos,
         "context overflow compaction failure preserves provider status and error body details");
  expect(transport.requests().size() == 2, "failed context overflow compaction does not retry provider call");
  expect(entries && count_compaction_entries(*entries) == 0, "failed context overflow compaction leaves no partial compaction entry");
}

void test_app_non_overflow_provider_error_does_not_compact_or_retry()
{
  auto const root = temp_root() / "app-non-overflow-error";
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
  expect(session.has_value(), "non-overflow provider error test opens runtime session");
  if (!session)
    return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport({ava::provider::HttpResponse{.status_code = 500, .headers = {}, .body = "server unavailable"}});
  ava::app::runtime::RunOptions run_options;
  run_options.access_token = "token";

  auto result = ava::app::run_prompt(*session, "server error", provider, transport, run_options);
  auto entries = session->store.load();
  expect(!result && result.error().message().find("OpenAI HTTP request failed") != std::string::npos, "non-overflow provider error is returned");
  expect(transport.requests().size() == 1, "non-overflow provider error does not retry");
  expect(entries && count_compaction_entries(*entries) == 0, "non-overflow provider error does not compact");

  auto context_error = ava::core::Error(ava::core::ErrorCategory::Provider, "too many tokens for context window");
  auto auth_error = ava::core::Error(ava::core::ErrorCategory::Provider, "authentication failed");
  expect(ava::provider::is_context_overflow_error(context_error) && !ava::provider::is_context_overflow_error(auth_error),
         "context overflow helper distinguishes token-window errors from unrelated provider errors");
}

void test_app_context_overflow_retry_is_bounded()
{
  auto const root = temp_root() / "app-context-overflow-bounded";
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
  expect(session.has_value(), "bounded overflow retry test opens runtime session");
  if (!session)
    return;

  ava::provider::OpenAIProvider const provider("https://api.example.test");
  ava::tests::FakeTransport transport(
      {ava::provider::HttpResponse{.status_code = 400, .headers = {}, .body = "{\"error\":{\"message\":\"context length exceeded token limit\"}}"},
       ava::provider::HttpResponse{.status_code = 200, .headers = {}, .body = "{\"output_text\":\"BOUNDED SUMMARY\"}"},
       ava::provider::HttpResponse{.status_code = 400, .headers = {}, .body = "{\"error\":{\"message\":\"context length exceeded token limit again\"}}"}});
  ava::app::runtime::RunOptions run_options;
  run_options.access_token = "token";

  auto result = ava::app::run_prompt(*session, "overflow twice", provider, transport, run_options);
  auto entries = session->store.load();
  expect(!result && ava::provider::is_context_overflow_error(result.error()), "second context overflow is returned instead of retried indefinitely");
  expect(transport.requests().size() == 3, "context overflow retry is attempted at most once");
  expect(entries && count_compaction_entries(*entries) == 1, "bounded context overflow retry appends one compaction entry");
}

}  // namespace

void run_app_compaction_tests()
{
  test_app_compact_provider_summary_success();
  test_app_compact_openai_oauth_streaming_summary_success();
  test_app_compact_provider_failure_leaves_session_untouched();
  test_compaction_observation_preserves_cancellation_callback_contract();
  test_app_auto_compaction_provider_cancellation_leaves_session_untouched();
  test_app_compact_oversized_summary_leaves_session_untouched();
  test_app_compact_cancellation_before_append_leaves_session_untouched();
  test_app_compaction_prompt_builder_sections();
  test_app_auto_compaction_appends_summary_and_rebuilds_context();
  test_app_auto_compaction_recent_context_respects_token_budget();
  test_app_auto_compaction_recent_context_truncates_utf8_safely();
  test_app_auto_compaction_explicit_zero_disables();
  test_app_auto_compaction_uses_default_threshold_without_context_window_metadata();
  test_app_auto_compaction_retries_stale_snapshot_before_append();
  test_app_auto_compaction_repeated_stale_snapshot_fails_without_append();
  test_app_context_overflow_compacts_and_retries_once_successfully();
  test_app_context_overflow_compaction_failure_leaves_no_partial_entry();
  test_app_non_overflow_provider_error_does_not_compact_or_retry();
  test_app_context_overflow_retry_is_bounded();
}
