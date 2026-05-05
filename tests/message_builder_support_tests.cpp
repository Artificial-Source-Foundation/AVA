#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "ava/agent/message_builder_support.h"
#include "ava/provider/provider.h"
#include "ava/session/session_store.h"
#include "tests/support/test_harness.h"

namespace {

ava::session::SessionEntry entry(ava::session::EntryType type, std::string data_json)
{
  return ava::session::SessionEntry{.id = "entry_test",
                                    .parent_id = "",
                                    .type = type,
                                    .timestamp = "2026-05-05T00:00:00Z",
                                    .data_json = std::move(data_json)};
}

void test_text_context_helpers()
{
  auto const user = entry(ava::session::EntryType::UserMessage, R"({"text":"hello from session","unused":"ignored"})");
  expect(ava::agent::detail::entry_text(user) == "hello from session",
         "message builder support extracts session text fields");

  auto const compaction = entry(ava::session::EntryType::Compaction,
                                R"({"summary":"prior summary","instructions":"carry this","recent_context":"tail"})");
  auto const context = ava::agent::detail::compaction_context_text(compaction);
  expect(context.find("prior summary") != std::string::npos && context.find("carry this") != std::string::npos &&
             context.find("tail") != std::string::npos,
         "message builder support formats compacted context from semantic fields");

  auto const missing = entry(ava::session::EntryType::Compaction, "{}");
  expect(ava::agent::detail::compaction_context_text(missing).find("summary is unavailable") != std::string::npos,
         "message builder support keeps a readable compaction fallback");

  auto const tool_call = entry(ava::session::EntryType::ToolCall,
                               R"({"call_id":"call_read","name":"read_file","arguments":"{\"path\":\"README.md\"}"})");
  auto const tool_result = entry(ava::session::EntryType::ToolResult,
                                 R"({"call_id":"call_read","name":"read_file","success":true,"result":"file text"})");
  expect(ava::agent::detail::tool_call_context_text(tool_call).find("arguments_json") != std::string::npos,
         "message builder support formats tool call context");
  expect(ava::agent::detail::tool_context_text(tool_result).find("file text") != std::string::npos,
         "message builder support formats tool result context");
}

void test_boolean_and_utf8_helpers()
{
  auto const truth = ava::agent::detail::bool_field(R"({"success":true})", "success");
  auto const falsehood = ava::agent::detail::bool_field(R"({"success":false})", "success");
  expect(truth && *truth, "message builder support parses JSON true booleans");
  expect(falsehood && !*falsehood, "message builder support parses JSON false booleans");
  expect(!ava::agent::detail::bool_field(R"({"success":"true"})", "success"),
         "message builder support rejects string booleans");
  expect(!ava::agent::detail::bool_field(R"({"success":truex})", "success"),
         "message builder support requires boolean terminators");

  auto const smile = std::string("\xF0\x9F\x99\x82");
  auto const payload = std::string("abc") + smile + "def";
  expect(ava::agent::detail::utf8_prefix_boundary(payload, 4) == 3,
         "message builder support does not split UTF-8 scalars");
  expect(ava::agent::detail::utf8_prefix_boundary(payload, 7) == 7,
         "message builder support accepts complete UTF-8 scalars");

  constexpr std::string_view marker = "\n[AVA: tool result content truncated]";
  auto const oversized_payload = std::string("abc") + smile + std::string(64, 'x');
  auto const truncated = ava::agent::detail::truncate_native_tool_result(oversized_payload, marker.size() + 4);
  expect(truncated == std::string("abc") + std::string(marker),
         "message builder support truncates native tool results on UTF-8 boundaries");
}

void test_tool_content_parts()
{
  auto const call = entry(ava::session::EntryType::ToolCall,
                          R"({"call_id":"call_read","name":"read_file","arguments":"{\"path\":\"README.md\"}"})");
  auto call_parts = ava::agent::detail::tool_call_content_parts(call);
  expect(call_parts.size() == 1 && call_parts.front().type == ava::provider::ContentPartType::ToolUse &&
             call_parts.front().tool_call_id == "call_read" && call_parts.front().tool_name == "read_file" &&
             call_parts.front().input_json == R"({"path":"README.md"})",
         "message builder support projects tool calls into native content parts");

  auto const bad_args =
      entry(ava::session::EntryType::ToolCall, R"({"call_id":"call_read","name":"read_file","arguments":"[1]"})");
  expect(ava::agent::detail::tool_call_content_parts(bad_args).empty(),
         "message builder support rejects non-object native tool arguments");

  auto const failed = entry(ava::session::EntryType::ToolResult,
                            R"({"call_id":"call_read","name":"read_file","success":false,"result":"tool failed"})");
  auto result_parts = ava::agent::detail::tool_result_content_parts(failed, 1024);
  expect(result_parts.size() == 1 && result_parts.front().type == ava::provider::ContentPartType::ToolResult &&
             result_parts.front().tool_call_id == "call_read" && result_parts.front().tool_name == "read_file" &&
             result_parts.front().text == "tool failed" && result_parts.front().is_error,
         "message builder support projects failed tool results into native content parts");
}

void test_reasoning_and_vector_helpers()
{
  auto const reasoning =
      entry(ava::session::EntryType::ReasoningBlock,
            R"({"text":"private","signature":"sig","redacted_data":"hidden","redacted":true,"format":"provider"})");
  auto part = ava::agent::detail::reasoning_content_part(reasoning);
  expect(part && part->type == ava::provider::ContentPartType::Reasoning && part->text.empty() &&
             part->reasoning_signature == "sig" && part->reasoning_redacted_data == "hidden" && part->redacted,
         "message builder support preserves redacted reasoning metadata without visible text");

  std::vector<ava::provider::ContentPart> target{
      ava::provider::ContentPart{.type = ava::provider::ContentPartType::Text, .text = "answer"}};
  std::vector<ava::provider::ContentPart> pending{
      ava::provider::ContentPart{.type = ava::provider::ContentPartType::Reasoning, .text = "thought"}};
  ava::agent::detail::append_pending_reasoning_parts(target, pending);
  expect(target.size() == 2 && pending.empty() && target.back().text == "thought",
         "message builder support moves pending reasoning parts into message parts");

  auto const assistant = entry(ava::session::EntryType::AssistantMessage, R"({"text":"answer","tool_calls":2})");
  auto const assistant_bad_count =
      entry(ava::session::EntryType::AssistantMessage, R"({"text":"answer","tool_calls":-1})");
  expect(ava::agent::detail::assistant_tool_call_count(assistant) == 2,
         "message builder support reads assistant tool call counts");
  expect(ava::agent::detail::assistant_tool_call_count(assistant_bad_count) == 0,
         "message builder support clamps invalid assistant tool call counts");

  std::string fallback;
  ava::agent::detail::append_fallback_text(fallback, "one");
  ava::agent::detail::append_fallback_text(fallback, "");
  ava::agent::detail::append_fallback_text(fallback, "two");
  expect(fallback == "one\n\ntwo", "message builder support joins fallback text blocks readably");

  std::vector<std::string> values{"a", "b", "a"};
  expect(ava::agent::detail::contains_string(values, "b"), "message builder support finds string values");
  expect(ava::agent::detail::erase_first_string(values, "a") && values == std::vector<std::string>({"b", "a"}),
         "message builder support erases only the first matching string value");
}

}  // namespace

void run_message_builder_support_tests()
{
  test_text_context_helpers();
  test_boolean_and_utf8_helpers();
  test_tool_content_parts();
  test_reasoning_and_vector_helpers();
}
