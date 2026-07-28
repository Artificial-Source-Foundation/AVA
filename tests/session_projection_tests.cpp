#include "sys.h"
#include "tests/session_test_declarations.h"
#include "tests/support/test_harness.h"
#include "ava/http/transport.h"
#include "ava/app/commands.h"
#include "ava/agent/message_builder.h"
#include "ava/session/assistant_output.h"
#include "ava/session/compaction.h"
#include "ava/session/export.h"
#include "ava/session/logical_projection.h"
#include "ava/session/record.h"
#include "ava/session/transcript.h"
#include "ava/session/validation.h"
#include "ava/provider/anthropic_provider.h"
#include "ava/provider/openai_provider.h"
#include "ava/provider/provider.h"
#include "ava/provider/provider_utils.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace session_tests {
void test_logical_session_projection_v4_public_privacy_and_compatibility()
{
  using ava::session::AssistantOutputFunctionCall;
  using ava::session::AssistantOutputItem;
  using ava::session::AssistantOutputItemKind;
  using ava::session::AssistantOutputReasoning;
  using ava::session::AssistantOutputText;
  using ava::session::AssistantOutputTextPhase;
  using ava::session::AssistantTurnCommit;
  using ava::session::EntryType;
  using ava::session::SessionEntry;

  auto item = [](std::string id, std::size_t sequence, AssistantOutputItemKind kind, ava::session::AssistantOutputItemPayload payload, std::string timestamp) {
    auto data =
        ava::session::serialize_assistant_output_item_data_json(AssistantOutputItem{.assistant_turn_id = "turn_projection",
                                                                                    .sequence = sequence,
                                                                                    .kind = kind,
                                                                                    .provider_item_id = "PRIVATE_PROVIDER_ITEM_ID_" + std::to_string(sequence),
                                                                                    .provider_output_index = sequence,
                                                                                    .payload = std::move(payload)});
    return SessionEntry{
        .id = std::move(id), .parent_id = "", .type = EntryType::AssistantOutputItem, .timestamp = std::move(timestamp), .data_json = data.value_or("{}")};
  };
  auto commit_data = ava::session::serialize_assistant_turn_commit_data_json(
      AssistantTurnCommit{.assistant_turn_id = "turn_projection",
                          .item_count = 4,
                          .provider = "openai",
                          .model = "gpt-5.5",
                          .api_family = "openai_responses",
                          .reasoning_format = "openai_responses",
                          .finish_reason = "tool_calls",
                          .usage_json = "{\"input_tokens\":7,\"output_tokens\":5,\"total_tokens\":12,\"source\":\"provider\"}"});
  std::string const sha256 = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
  std::vector<SessionEntry> const entries = {
      SessionEntry{
          .id = "legacy_user",
          .parent_id = "",
          .type = EntryType::UserMessage,
          .timestamp = "2026-07-18T00:00:00Z",
          .data_json = "{\"text\":\"legacy "
                       "user\",\"attachments\":[{\"id\":\"image_projection\",\"type\":\"image\",\"mime_type\":\"image/png\",\"byte_size\":12,\"sha256\":\"" +
                       sha256 + "\",\"storage_path\":\"attachments/private-source.png\"}]}",
          .version = 0},
      item("out_commentary", 0, AssistantOutputItemKind::Text,
           AssistantOutputText{.text = "commentary ", .assistant_phase = AssistantOutputTextPhase::Commentary}, "2026-07-18T00:00:01Z"),
      item("out_reasoning", 1, AssistantOutputItemKind::Reasoning,
           AssistantOutputReasoning{.text = "visible reasoning",
                                    .format = "openai_responses",
                                    .redacted = false,
                                    .signature = "PRIVATE_SIGNATURE_PROJECTION",
                                    .redacted_data = "PRIVATE_REDACTED_PROJECTION",
                                    .native_item_json = "{\"id\":\"PRIVATE_NATIVE_REASONING_ID\",\"type\":\"reasoning\",\"summary\":[]}"},
           "2026-07-18T00:00:02Z"),
      item("out_function", 2, AssistantOutputItemKind::FunctionCall,
           AssistantOutputFunctionCall{.call_id = "call_projection", .name = "read_file", .arguments_json = "{\"path\":\"README.md\"}"},
           "2026-07-18T00:00:03Z"),
      item("out_final", 3, AssistantOutputItemKind::Text, AssistantOutputText{.text = "final", .assistant_phase = AssistantOutputTextPhase::FinalAnswer},
           "2026-07-18T00:00:04Z"),
      SessionEntry{.id = "commit_projection",
                   .parent_id = "out_final",
                   .type = EntryType::AssistantTurnCommit,
                   .timestamp = "2026-07-18T00:00:05Z",
                   .data_json = commit_data.value_or("{}")},
      SessionEntry{.id = "result_projection",
                   .parent_id = "commit_projection",
                   .type = EntryType::ToolResult,
                   .timestamp = "2026-07-18T00:00:06Z",
                   .data_json =
                       "{\"assistant_output_entry_id\":\"out_function\",\"call_id\":\"call_projection\",\"name\":\"read_file\",\"success\":true,\"status\":"
                       "\"success\",\"result\":\"read "
                       "ok\",\"structured_result\":{\"schema_version\":1,\"call_id\":\"call_projection\",\"tool\":\"read_file\",\"status\":\"success\",\"ok\":"
                       "true,\"content_type\":\"text/plain\",\"content\":\"read ok\"},\"provider_private\":\"PRIVATE_RESULT_CANARY\"}"},
      SessionEntry{.id = "tail_projection",
                   .parent_id = "result_projection",
                   .type = EntryType::AssistantOutputItem,
                   .timestamp = "2026-07-18T00:00:07Z",
                   .data_json = "{\"schema_version\":1,\"assistant_turn_id\":\"tail_projection\",\"sequence\":0,\"kind\":\"text\",\"text\":\"INCOMPLETE_"
                                "STAGING_CANARY\",\"assistant_phase\":\"commentary\"}"},
  };

  auto projected = ava::session::project_logical_session_history(entries);
  auto ordered = ava::session::project_ordered_public_session_history(entries);
  auto jsonl = ava::session::format_session_portable_jsonl_checked(entries);
  auto markdown = ava::session::format_session_markdown_checked(entries, ava::session::ExportOptions{.include_tool_details = true});
  auto html = ava::session::format_session_html_checked(entries, ava::session::ExportOptions{.include_tool_details = true});
  auto transcript = ava::session::project_transcript(entries);
  auto estimated = ava::session::estimate_session_tokens(entries);
  ava::session::CompactionConfig threshold_config = ava::session::default_compaction_config();
  threshold_config.auto_threshold_tokens = estimated.value_or(0);
  auto threshold_decision = ava::session::should_auto_compact(entries, threshold_config);
  auto compaction_prompt = ava::app::build_compaction_summary_prompt(entries, threshold_config, "", 42);

  bool parsed_jsonl = jsonl.has_value();
  std::vector<SessionEntry> imported;
  if (jsonl)
  {
    std::istringstream input(*jsonl);
    std::string line;
    while (std::getline(input, line))
    {
      auto parsed = ava::session::parse_session_entry_line(line, "portable-v4-projection.jsonl");
      if (!parsed)
      {
        parsed_jsonl = false;
        break;
      }
      imported.push_back(std::move(*parsed));
    }
  }
  auto const replay = ava::session::validate_session_replay(imported);
  auto imported_messages = ava::agent::build_provider_messages_from_entries(imported);
  auto imported_exact_target_messages = ava::agent::build_provider_messages_from_entries(
      imported, ava::agent::MessageBuildOptions{.target = ava::agent::HistoryReplayTarget{.provider_id = "openai",
                                                                                          .model_id = "gpt-5.5",
                                                                                          .api_family = "openai_responses",
                                                                                          .reasoning_format = "openai_responses",
                                                                                          .supports_tools = true,
                                                                                          .supports_images = false}});

  bool compatibility_shape =
      projected && projected->size() == 5 && (*projected)[0].type == EntryType::UserMessage && (*projected)[1].type == EntryType::ReasoningBlock &&
      (*projected)[1].id == "out_reasoning" && (*projected)[1].timestamp == "2026-07-18T00:00:02Z" && (*projected)[2].type == EntryType::AssistantMessage &&
      (*projected)[2].id == "commit_projection" && (*projected)[2].timestamp == "2026-07-18T00:00:05Z" &&
      (*projected)[2].data_json.find("commentary final") != std::string::npos && (*projected)[2].data_json.find("\"tool_calls\":1") != std::string::npos &&
      (*projected)[2].data_json.find("\"input_tokens\":7") != std::string::npos &&
      (*projected)[2].data_json.find("\"ordered_output\":[{") != std::string::npos &&
      (*projected)[2].data_json.find("\"assistant_phase\":\"commentary\"") != std::string::npos &&
      (*projected)[2].data_json.find("\"assistant_phase\":\"final_answer\"") != std::string::npos && (*projected)[3].type == EntryType::ToolCall &&
      (*projected)[3].id == "out_function" && (*projected)[3].timestamp == "2026-07-18T00:00:03Z" && (*projected)[4].type == EntryType::ToolResult &&
      (*projected)[4].data_json.find("assistant_output_entry_id") == std::string::npos &&
      (*projected)[4].data_json.find("PRIVATE_RESULT_CANARY") == std::string::npos;
  bool ordered_shape = ordered && ordered->size() == 6 && (*ordered)[0].type == EntryType::UserMessage && (*ordered)[1].type == EntryType::AssistantMessage &&
                       (*ordered)[1].id == "out_commentary" && (*ordered)[1].data_json.find("\"assistant_phase\":\"commentary\"") != std::string::npos &&
                       (*ordered)[2].type == EntryType::ReasoningBlock && (*ordered)[2].id == "out_reasoning" && (*ordered)[3].type == EntryType::ToolCall &&
                       (*ordered)[3].id == "out_function" && (*ordered)[4].type == EntryType::AssistantMessage && (*ordered)[4].id == "out_final" &&
                       (*ordered)[4].data_json.find("\"assistant_phase\":\"final_answer\"") != std::string::npos && (*ordered)[5].type == EntryType::ToolResult;
  bool portable_shape = imported.size() == 7 && imported[0].type == EntryType::UserMessage && imported[0].version == 0 &&
                        imported[1].type == EntryType::AssistantOutputItem && imported[1].data_json.find("provider_item_id") == std::string::npos &&
                        imported[2].type == EntryType::AssistantOutputItem && imported[3].type == EntryType::AssistantOutputItem &&
                        imported[4].type == EntryType::AssistantOutputItem && imported[5].type == EntryType::AssistantTurnCommit &&
                        imported[6].type == EntryType::ToolResult &&
                        imported[6].data_json.find("\"assistant_output_entry_id\":\"out_function\"") != std::string::npos;
  ava::provider::OpenAIProvider const portable_openai_provider("https://api.example.test");
  ava::provider::AnthropicProvider const portable_anthropic_provider("https://anthropic.example.test");
  std::optional<ava::http::HttpRequest> portable_openai_request;
  std::optional<ava::http::HttpRequest> portable_anthropic_request;
  if (imported_messages)
  {
    auto openai_request = portable_openai_provider.build_request(
        ava::provider::ProviderRequest{
            .provider_id = "openai", .model_id = "gpt-5.5", .system_prompt = "system", .messages = *imported_messages, .tools_json = {}, .stream = false},
        "test-key");
    auto anthropic_request = portable_anthropic_provider.build_request(ava::provider::ProviderRequest{.provider_id = "anthropic",
                                                                                                      .model_id = "claude-sonnet-4-5",
                                                                                                      .system_prompt = "system",
                                                                                                      .messages = *imported_messages,
                                                                                                      .tools_json = {},
                                                                                                      .stream = false},
                                                                       "test-key");
    if (openai_request)
      portable_openai_request = std::move(*openai_request);
    if (anthropic_request)
      portable_anthropic_request = std::move(*anthropic_request);
  }
  bool provider_replay_order =
      imported_messages && imported_messages->size() == 3 && (*imported_messages)[1].role == "assistant" && (*imported_messages)[1].content_parts.size() == 3 &&
      (*imported_messages)[1].content_parts[0].type == ava::provider::ContentPartType::Text && (*imported_messages)[1].content_parts[0].text == "commentary " &&
      (*imported_messages)[1].content_parts[1].type == ava::provider::ContentPartType::Text &&
      (*imported_messages)[1].content_parts[1].text.find("Tool call (read_file)") != std::string::npos &&
      (*imported_messages)[1].content_parts[2].type == ava::provider::ContentPartType::Text && (*imported_messages)[1].content_parts[2].text == "final" &&
      (*imported_messages)[1].content.find("visible reasoning") == std::string::npos &&
      (*imported_messages)[1].content.find("Tool call (read_file)") != std::string::npos && (*imported_messages)[2].role == "user" &&
      (*imported_messages)[2].content_parts.empty();
  bool exact_target_portable_archive =
      imported_exact_target_messages && imported_exact_target_messages->size() == 3 && (*imported_exact_target_messages)[1].content_parts.size() == 3 &&
      (*imported_exact_target_messages)[1].content_parts[0].type == ava::provider::ContentPartType::Text &&
      (*imported_exact_target_messages)[1].content_parts[1].type == ava::provider::ContentPartType::ToolUse &&
      (*imported_exact_target_messages)[1].content_parts[1].tool_call_id != "call_projection" &&
      (*imported_exact_target_messages)[1].content_parts[2].type == ava::provider::ContentPartType::Text &&
      (*imported_exact_target_messages)[2].content_parts.size() == 1 &&
      (*imported_exact_target_messages)[2].content_parts[0].type == ava::provider::ContentPartType::ToolResult &&
      (*imported_exact_target_messages)[2].content_parts[0].tool_call_id == (*imported_exact_target_messages)[1].content_parts[1].tool_call_id &&
      (*imported_exact_target_messages)[1].content.find("visible reasoning") == std::string::npos;
  auto markdown_commentary = markdown ? markdown->find("commentary ") : std::string::npos;
  auto markdown_reasoning = markdown ? markdown->find("visible reasoning") : std::string::npos;
  auto markdown_function = markdown ? markdown->find("read_file") : std::string::npos;
  auto markdown_final = markdown ? markdown->find("final") : std::string::npos;
  auto html_commentary = html ? html->find("commentary ") : std::string::npos;
  auto html_reasoning = html ? html->find("visible reasoning") : std::string::npos;
  auto html_function = html ? html->find("read_file") : std::string::npos;
  auto html_final = html ? html->find("final") : std::string::npos;
  bool private_values_absent =
      jsonl && markdown && html && compaction_prompt && jsonl->starts_with("{\"id\":\"legacy_user\"") &&
      jsonl->find("PRIVATE_PROVIDER_ITEM_ID") == std::string::npos && jsonl->find("PRIVATE_SIGNATURE_PROJECTION") == std::string::npos &&
      jsonl->find("PRIVATE_REDACTED_PROJECTION") == std::string::npos && jsonl->find("PRIVATE_NATIVE_REASONING_ID") == std::string::npos &&
      jsonl->find("INCOMPLETE_STAGING_CANARY") == std::string::npos && jsonl->find("attachments/private-source.png") == std::string::npos &&
      jsonl->find("attachments/portable-redacted") != std::string::npos && markdown->find("PRIVATE_SIGNATURE_PROJECTION") == std::string::npos &&
      html->find("PRIVATE_REDACTED_PROJECTION") == std::string::npos && compaction_prompt->find("PRIVATE_SIGNATURE_PROJECTION") == std::string::npos &&
      compaction_prompt->find("PRIVATE_NATIVE_REASONING_ID") == std::string::npos && compaction_prompt->find("visible reasoning") == std::string::npos;
  bool rendered_order = markdown_commentary < markdown_reasoning && markdown_reasoning < markdown_function && markdown_function < markdown_final &&
                        html_commentary < html_reasoning && html_reasoning < html_function && html_function < html_final &&
                        compaction_prompt->find("commentary ") < compaction_prompt->find("read_file") &&
                        compaction_prompt->find("read_file") < compaction_prompt->find("final");
  expect(compatibility_shape && ordered_shape && portable_shape && parsed_jsonl && replay.ok() && provider_replay_order && exact_target_portable_archive &&
             private_values_absent && portable_openai_request && portable_anthropic_request &&
             portable_openai_request->body.find("PRIVATE_SIGNATURE_PROJECTION") == std::string::npos &&
             portable_openai_request->body.find("PRIVATE_NATIVE_REASONING_ID") == std::string::npos &&
             portable_anthropic_request->body.find("PRIVATE_REDACTED_PROJECTION") == std::string::npos &&
             portable_anthropic_request->body.find("redacted_thinking") == std::string::npos && rendered_order && transcript && transcript->size() == 3 &&
             (*transcript)[1].role == ava::session::TranscriptRole::Assistant && (*transcript)[1].text == "commentary " &&
             (*transcript)[2].role == ava::session::TranscriptRole::Assistant && (*transcript)[2].text == "final" && estimated && *estimated > 0 &&
             threshold_decision && threshold_decision->should_compact,
         "v4 compatibility and public projections preserve order while request and compaction projections drop reasoning and private data");

  auto malformed = entries;
  malformed[5].data_json =
      "{\"schema_version\":1,\"assistant_turn_id\":\"turn_projection\",\"item_count\":3,\"provider\":\"openai\",\"model\":\"gpt-5.5\",\"finish_reason\":\"tool_"
      "calls\"}";
  auto malformed_projection = ava::session::project_logical_session_history(malformed);
  std::vector<SessionEntry> const legacy_v3 = {
      SessionEntry{.id = "legacy_v3_user",
                   .parent_id = "",
                   .type = EntryType::UserMessage,
                   .timestamp = "2026-07-18T01:00:00Z",
                   .data_json = "{\"text\":\"v3\"}",
                   .version = 3},
      SessionEntry{.id = "legacy_v3_assistant",
                   .parent_id = "",
                   .type = EntryType::AssistantMessage,
                   .timestamp = "2026-07-18T01:00:01Z",
                   .data_json = "{\"text\":\"unchanged\"}",
                   .version = 3},
  };
  auto legacy_projection = ava::session::project_logical_session_history(legacy_v3);
  expect(!malformed_projection && legacy_projection && legacy_projection->size() == legacy_v3.size() && (*legacy_projection)[0].id == legacy_v3[0].id &&
             (*legacy_projection)[0].version == 3 && (*legacy_projection)[0].data_json == legacy_v3[0].data_json &&
             (*legacy_projection)[1].id == legacy_v3[1].id && (*legacy_projection)[1].version == 3 &&
             (*legacy_projection)[1].data_json == legacy_v3[1].data_json,
         "logical projection rejects malformed v4 transactions while preserving v3 records unchanged");
}

void test_provider_base64_encoding()
{
  expect(ava::provider::base64_encode("") == "", "base64 encoder handles empty input");
  expect(ava::provider::base64_encode(std::string_view("\0", 1)) == "AA==", "base64 encoder handles one-byte input padding");
  expect(ava::provider::base64_encode("a") == "YQ==", "base64 encoder handles text one-byte input");
  expect(ava::provider::base64_encode("ab") == "YWI=", "base64 encoder handles two-byte input padding");
  expect(ava::provider::base64_encode("abc") == "YWJj", "base64 encoder handles full triples");
  expect(ava::provider::base64_encode("hello") == "aGVsbG8=", "base64 encoder handles multi-block input");
}

}  // namespace session_tests
