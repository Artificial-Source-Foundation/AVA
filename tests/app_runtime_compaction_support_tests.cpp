#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ava/app/runtime.h"
#include "ava/app/runtime_compaction_support.h"
#include "ava/provider/provider.h"
#include "ava/session/compaction.h"
#include "ava/session/session_store.h"
#include "tests/support/test_harness.h"

namespace {

ava::session::SessionEntry entry(std::string id, ava::session::EntryType type, std::string data_json)
{
  return ava::session::SessionEntry{.id = std::move(id),
                                    .parent_id = "",
                                    .type = type,
                                    .timestamp = "2026-05-05T00:00:00Z",
                                    .data_json = std::move(data_json)};
}

class StaticProvider final : public ava::provider::Provider {
 public:
  explicit StaticProvider(std::vector<ava::provider::StreamEvent> events) : events_(std::move(events)) {}

  [[nodiscard]] ava::core::Result<ava::provider::HttpRequest> build_request(ava::provider::ProviderRequest const&,
                                                                            std::string_view) const override
  {
    ava::provider::HttpRequest request;
    request.method = "POST";
    request.url = "https://example.invalid";
    request.body = "{}";
    return request;
  }

  [[nodiscard]] ava::core::Result<std::vector<ava::provider::StreamEvent>> parse_response(
      ava::provider::HttpResponse const&, bool) const override
  {
    return events_;
  }

 private:
  std::vector<ava::provider::StreamEvent> events_;
};

ava::provider::StreamEvent text_delta(std::string text)
{
  ava::provider::StreamEvent event;
  event.type = ava::provider::StreamEventType::TextDelta;
  event.text = std::move(text);
  return event;
}

ava::provider::StreamEvent stream_error(std::string message)
{
  ava::provider::StreamEvent event;
  event.type = ava::provider::StreamEventType::Error;
  event.error_message = std::move(message);
  return event;
}

ava::provider::HttpResponse http_response(std::string body)
{
  ava::provider::HttpResponse response;
  response.status_code = 200;
  response.body = std::move(body);
  return response;
}

void test_reasoning_sanitization_and_prompt()
{
  auto const redacted_reasoning = entry(
      "entry_reasoning", ava::session::EntryType::ReasoningBlock,
      R"({"provider":"openai","model":"gpt-5.5","format":"encrypted","text":"secret chain","signature":"sig","redacted":true})");
  auto const sanitized = ava::app::detail::sanitized_reasoning_data_for_compaction(redacted_reasoning);
  expect(sanitized.find("secret chain") == std::string::npos &&
             sanitized.find("\"redacted\":true") != std::string::npos &&
             sanitized.find("\"signature_present\":true") != std::string::npos,
         "runtime compaction support omits redacted reasoning text while preserving semantic metadata");

  auto config = ava::session::default_compaction_config();
  config.model_id = "gpt-5.5-compact";
  config.max_summary_bytes = 2048;
  config.auto_threshold_tokens = 1024;
  config.keep_recent_messages = 3;
  config.keep_recent_tokens = 128;
  std::vector<ava::session::SessionEntry> entries{
      entry("entry_user", ava::session::EntryType::UserMessage, R"({"text":"keep this user request"})"),
      entry("entry_replay", ava::session::EntryType::UserMessage,
            R"({"text":"internal replay","internal_replay":true})"),
      redacted_reasoning};

  auto const prompt = ava::app::build_compaction_summary_prompt(entries, config, "preserve files", 42);
  expect(prompt.find("preserve files") != std::string::npos &&
             prompt.find("keep this user request") != std::string::npos &&
             prompt.find("internal replay") == std::string::npos && prompt.find("secret chain") == std::string::npos &&
             prompt.find("gpt-5.5-compact") != std::string::npos,
         "runtime compaction support builds prompts from semantic session entries and skips internal replays");
}

void test_recent_context_truncation_and_replay_filtering()
{
  auto const smile = std::string("\xF0\x9F\x99\x82");
  auto const utf8_text = std::string("abc") + smile + "tail";
  expect(ava::app::detail::utf8_suffix_start(utf8_text, 5) == 7,
         "runtime compaction support starts UTF-8 suffixes on scalar boundaries");

  auto const truncated =
      ava::app::detail::truncate_recent_context_to_token_budget(std::string(160, 'x') + smile + "tail", 20);
  expect(truncated.find("[AVA: recent context tail truncated") == 0 && truncated.ends_with("tail"),
         "runtime compaction support truncates recent context tails with a readable marker");

  std::vector<ava::provider::ChatMessage> messages{ava::provider::ChatMessage{.role = "user", .content = "first"},
                                                   ava::provider::ChatMessage{.role = "assistant", .content = "answer"},
                                                   ava::provider::ChatMessage{.role = "user", .content = "replayed"}};
  ava::app::detail::erase_replayed_active_user_messages(messages, {"replayed"});
  expect(messages.size() == 2 && messages.back().content == "answer",
         "runtime compaction support removes replayed active user messages from recent context");

  std::vector<ava::session::SessionEntry> entries{
      entry("entry_user_1", ava::session::EntryType::UserMessage, R"({"text":"first"})"),
      entry("entry_user_2", ava::session::EntryType::UserMessage, R"({"text":"replayed"})"),
      entry("entry_assistant", ava::session::EntryType::AssistantMessage, R"({"text":"answer"})")};
  auto tail = ava::app::detail::build_recent_context_tail(entries, 3, 128, {"replayed"});
  expect(tail && tail->find("first") != std::string::npos && tail->find("answer") != std::string::npos &&
             tail->find("replayed") == std::string::npos,
         "runtime compaction support builds recent context after replay filtering");
}

void test_snapshot_and_response_helpers()
{
  std::vector<ava::session::SessionEntry> const first{
      entry("entry_user", ava::session::EntryType::UserMessage, R"({"text":"hello"})")};
  auto changed = first;
  changed.front().data_json = R"({"text":"changed"})";
  expect(ava::app::same_session_snapshot(first, first), "runtime compaction support compares matching snapshots");
  expect(!ava::app::same_session_snapshot(first, changed), "runtime compaction support detects changed snapshot data");

  auto const stale = ava::app::stale_compaction_snapshot_error("auto", 2, 3).format();
  expect(stale.find("trigger: auto") != std::string::npos && stale.find("snapshot_entries: 2") != std::string::npos &&
             stale.find("current_entries: 3") != std::string::npos,
         "runtime compaction support formats stale snapshot error context");

  StaticProvider text_provider({text_delta("sum"), text_delta("mary")});
  auto parsed = ava::app::detail::parse_compaction_response_text(text_provider, http_response("{}"), false);
  expect(parsed && *parsed == "summary", "runtime compaction support joins provider text deltas");

  StaticProvider error_provider({stream_error("bad stream")});
  auto failed = ava::app::detail::parse_compaction_response_text(error_provider, http_response("{}"), true);
  expect(!failed && failed.error().format().find("bad stream") != std::string::npos,
         "runtime compaction support surfaces provider stream errors");
}

}  // namespace

void run_app_runtime_compaction_support_tests()
{
  test_reasoning_sanitization_and_prompt();
  test_recent_context_truncation_and_replay_filtering();
  test_snapshot_and_response_helpers();
}
