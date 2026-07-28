#include "sys.h"
#include "tests/provider_openai_test_suite.h"
#include "tests/support/test_harness.h"
#include "ava/http/transport.h"
#include "ava/agent/agent_loop.h"
#include "ava/agent/stream_bridge.h"
#include "ava/provider/openai_response_parser.h"
#include "ava/provider/provider.h"
#include "ava/core/result.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <ranges>
#include <string>
#include <utility>
#include <vector>

namespace ava::tests::provider_openai_suite {

void exercise_contract_sse_validation(ava::provider::OpenAIProvider const& provider)
{
  std::string const sse =
      "data: {\"type\":\"response.output_text.delta\",\"delta\":\"hi\"}\r\n\r\n"
      "data: {\"type\":\"response.function_call.added\",\"call_id\":\"call_1\",\"name\":\"read_file\"}\r\n\r\n"
      "data: {\"type\":\"response.function_call_arguments.delta\",\"call_id\":\"call_1\",\"delta\":\"{}\"}\n\n"
      "data: {\"type\":\"response.function_call.done\",\"call_id\":\"call_1\"}\n\n"
      "data: [DONE]\n\n";
  auto events = ava::provider::parse_openai_sse(sse);
  expect(events.has_value(), "OpenAI SSE parses");
  if (events)
  {
    expect(events->size() == 5, "OpenAI SSE produces expected events");
    expect((*events)[0].type == ava::provider::StreamEventType::TextDelta && (*events)[0].text == "hi", "OpenAI SSE text delta parses");
    expect((*events)[1].type == ava::provider::StreamEventType::ToolCallStart && (*events)[1].tool_name == "read_file", "OpenAI SSE tool start parses");
    expect((*events)[4].type == ava::provider::StreamEventType::Done, "OpenAI SSE done parses");
  }
  auto output_item_tool = ava::provider::parse_openai_sse(
      "data: {\"type\":\"response.output_item.added\",\"item\":{\"id\":\"fc_live\","
      "\"type\":\"function_call\",\"name\":\"read_file\",\"call_id\":\"call_live_provider\",\"arguments\":\"\"}}\n\n"
      "data: {\"type\":\"response.function_call_arguments.delta\",\"item_id\":\"fc_live\","
      "\"delta\":\"{\\\"path\\\":\\\"smoke.txt\\\"}\"}\n\n"
      "data: {\"type\":\"response.function_call_arguments.done\",\"item_id\":\"fc_live\","
      "\"arguments\":\"{\\\"path\\\":\\\"smoke.txt\\\"}\"}\n\n"
      "data: {\"type\":\"response.output_item.done\",\"item\":{\"id\":\"fc_live\",\"type\":\"function_call\","
      "\"name\":\"read_file\",\"call_id\":\"call_live_provider\",\"arguments\":\"{\\\"path\\\":\\\"smoke.txt\\\"}\"}}\n\n"
      "data: [DONE]\n\n");
  expect(output_item_tool && output_item_tool->size() == 4 && (*output_item_tool)[0].type == ava::provider::StreamEventType::ToolCallStart &&
             (*output_item_tool)[0].tool_call_id == "call_live_provider" && (*output_item_tool)[0].tool_name == "read_file" &&
             (*output_item_tool)[1].type == ava::provider::StreamEventType::ToolCallDelta && (*output_item_tool)[1].tool_call_id == "call_live_provider" &&
             (*output_item_tool)[1].text.find("smoke.txt") != std::string::npos && (*output_item_tool)[2].type == ava::provider::StreamEventType::ToolCallEnd &&
             (*output_item_tool)[2].tool_call_id == "call_live_provider",
         "OpenAI stream parser maps function-call item IDs to logical call IDs across lifecycle events");

  auto post_terminal_tool = ava::provider::parse_openai_sse(
      "data: {\"type\":\"response.completed\",\"response\":{\"status\":\"completed\"}}\n\n"
      "data: {\"type\":\"response.output_item.done\",\"item\":{\"id\":\"fc_late\",\"type\":\"function_call\","
      "\"call_id\":\"call_late\",\"name\":\"list_directory\",\"arguments\":\"{}\"}}\n\n");
  expect(post_terminal_tool &&
             std::none_of(post_terminal_tool->begin(), post_terminal_tool->end(),
                          [](auto const& event) { return event.type == ava::provider::StreamEventType::ToolCallStart; }) &&
             std::any_of(post_terminal_tool->begin(), post_terminal_tool->end(),
                         [](auto const& event) { return event.type == ava::provider::StreamEventType::Error; }),
         "OpenAI stream parser rejects documented function calls emitted after a terminal response event");

  auto documented_added_terminal = ava::provider::parse_openai_sse(
      "data: {\"type\":\"response.output_item.added\",\"item\":{\"id\":\"fc_open\",\"type\":\"function_call\","
      "\"call_id\":\"call_open\",\"name\":\"read_file\",\"arguments\":\"\"}}\n\n"
      "data: {\"type\":\"response.completed\",\"response\":{\"status\":\"completed\"}}\n\n");
  expect(documented_added_terminal &&
             std::any_of(documented_added_terminal->begin(), documented_added_terminal->end(),
                         [](auto const& event) { return event.type == ava::provider::StreamEventType::ToolCallStart; }) &&
             std::none_of(documented_added_terminal->begin(), documented_added_terminal->end(),
                          [](auto const& event) { return event.type == ava::provider::StreamEventType::Done; }) &&
             std::any_of(documented_added_terminal->begin(), documented_added_terminal->end(),
                         [](auto const& event) { return event.type == ava::provider::StreamEventType::Error; }),
         "OpenAI documented output_item.added cannot reach a successful terminal before output_item.done");
  auto documented_added_incomplete = ava::provider::parse_openai_sse(
      "data: {\"type\":\"response.output_item.added\",\"item\":{\"id\":\"fc_open_incomplete\",\"type\":\"function_call\","
      "\"call_id\":\"call_open_incomplete\",\"name\":\"read_file\",\"arguments\":\"\"}}\n\n"
      "data: {\"type\":\"response.incomplete\",\"response\":{\"status\":\"incomplete\"}}\n\n");
  expect(documented_added_incomplete &&
             std::none_of(documented_added_incomplete->begin(), documented_added_incomplete->end(),
                          [](auto const& event) { return event.type == ava::provider::StreamEventType::Done; }) &&
             std::any_of(documented_added_incomplete->begin(), documented_added_incomplete->end(),
                         [](auto const& event) { return event.type == ava::provider::StreamEventType::Error; }),
         "OpenAI response.incomplete rejects an unfinished documented function item");
  auto documented_added_done = ava::provider::parse_openai_sse(
      "data: {\"type\":\"response.output_item.added\",\"item\":{\"id\":\"fc_open_done\",\"type\":\"function_call\","
      "\"call_id\":\"call_open_done\",\"name\":\"read_file\",\"arguments\":\"\"}}\n\n"
      "data: [DONE]\n\n");
  expect(documented_added_done &&
             std::none_of(documented_added_done->begin(), documented_added_done->end(),
                          [](auto const& event) { return event.type == ava::provider::StreamEventType::Done; }) &&
             std::any_of(documented_added_done->begin(), documented_added_done->end(),
                         [](auto const& event) { return event.type == ava::provider::StreamEventType::Error; }),
         "OpenAI [DONE] rejects an unfinished documented function item");
  auto documented_added_eof = ava::provider::parse_openai_sse(
      "data: {\"type\":\"response.output_item.added\",\"item\":{\"id\":\"fc_open_eof\",\"type\":\"function_call\","
      "\"call_id\":\"call_open_eof\",\"name\":\"read_file\",\"arguments\":\"\"}}\n\n");
  expect(documented_added_eof && std::any_of(documented_added_eof->begin(), documented_added_eof->end(),
                                             [](auto const& event) {
                                               return event.type == ava::provider::StreamEventType::Error &&
                                                      event.error_message.find("item completion") != std::string::npos;
                                             }),
         "OpenAI finish fallback rejects an unfinished documented function item");

  auto empty_final_arguments = ava::provider::parse_openai_sse(
      "data: {\"type\":\"response.output_item.added\",\"item\":{\"id\":\"fc_empty_final\",\"type\":\"function_call\","
      "\"call_id\":\"call_empty_final\",\"name\":\"read_file\",\"arguments\":\"\"}}\n\n"
      "data: {\"type\":\"response.output_item.done\",\"item\":{\"id\":\"fc_empty_final\",\"type\":\"function_call\","
      "\"call_id\":\"call_empty_final\",\"name\":\"read_file\",\"arguments\":\"\"}}\n\n");
  auto malformed_final_arguments = ava::provider::parse_openai_sse(
      "data: {\"type\":\"response.output_item.done\",\"item\":{\"id\":\"fc_bad_final\",\"type\":\"function_call\","
      "\"call_id\":\"call_bad_final\",\"name\":\"read_file\",\"arguments\":\"{\"}}\n\n");
  expect(empty_final_arguments && malformed_final_arguments &&
             std::none_of(empty_final_arguments->begin(), empty_final_arguments->end(),
                          [](auto const& event) { return event.type == ava::provider::StreamEventType::ToolCallEnd; }) &&
             std::none_of(malformed_final_arguments->begin(), malformed_final_arguments->end(),
                          [](auto const& event) { return event.type == ava::provider::StreamEventType::ToolCallEnd; }) &&
             std::any_of(empty_final_arguments->begin(), empty_final_arguments->end(),
                         [](auto const& event) { return event.type == ava::provider::StreamEventType::Error; }) &&
             std::any_of(malformed_final_arguments->begin(), malformed_final_arguments->end(),
                         [](auto const& event) { return event.type == ava::provider::StreamEventType::Error; }),
         "OpenAI output_item.done rejects empty or malformed final function arguments before ToolCallEnd");

  auto documented_done_missing_arguments = ava::provider::parse_openai_sse(
      "data: {\"type\":\"response.output_item.added\",\"item\":{\"id\":\"fc_done_missing\",\"type\":\"function_call\","
      "\"call_id\":\"call_done_missing\",\"name\":\"read_file\",\"arguments\":\"\"}}\n\n"
      "data: {\"type\":\"response.function_call_arguments.done\",\"item_id\":\"fc_done_missing\"}\n\n");
  auto documented_done_wrong_arguments = ava::provider::parse_openai_sse(
      "data: {\"type\":\"response.output_item.added\",\"item\":{\"id\":\"fc_done_wrong\",\"type\":\"function_call\","
      "\"call_id\":\"call_done_wrong\",\"name\":\"read_file\",\"arguments\":\"\"}}\n\n"
      "data: {\"type\":\"response.function_call_arguments.done\",\"item_id\":\"fc_done_wrong\",\"arguments\":{}}\n\n");
  auto documented_delta_missing = ava::provider::parse_openai_sse(
      "data: {\"type\":\"response.output_item.added\",\"item\":{\"id\":\"fc_delta_missing\",\"type\":\"function_call\","
      "\"call_id\":\"call_delta_missing\",\"name\":\"read_file\",\"arguments\":\"\"}}\n\n"
      "data: {\"type\":\"response.function_call_arguments.delta\",\"item_id\":\"fc_delta_missing\"}\n\n");
  auto documented_delta_wrong = ava::provider::parse_openai_sse(
      "data: {\"type\":\"response.output_item.added\",\"item\":{\"id\":\"fc_delta_wrong\",\"type\":\"function_call\","
      "\"call_id\":\"call_delta_wrong\",\"name\":\"read_file\",\"arguments\":\"\"}}\n\n"
      "data: {\"type\":\"response.function_call_arguments.delta\",\"item_id\":\"fc_delta_wrong\",\"delta\":{}}\n\n");
  auto has_error = [](auto const& parsed) {
    return parsed && std::any_of(parsed->begin(), parsed->end(), [](auto const& event) { return event.type == ava::provider::StreamEventType::Error; });
  };
  expect(has_error(documented_done_missing_arguments) && has_error(documented_done_wrong_arguments) && has_error(documented_delta_missing) &&
             has_error(documented_delta_wrong),
         "OpenAI documented function argument delta and completion events require present string fields");

  auto documented_missing_call_id = ava::provider::parse_openai_sse(
      "data: "
      "{\"type\":\"response.output_item.added\",\"item\":{\"id\":\"fc_missing_call\",\"type\":\"function_call\",\"name\":\"read_file\",\"arguments\":"
      "\"\"}}\n\n"
      "data: [DONE]\n\n");
  expect(documented_missing_call_id &&
             std::none_of(documented_missing_call_id->begin(), documented_missing_call_id->end(),
                          [](auto const& event) { return event.type == ava::provider::StreamEventType::ToolCallStart; }) &&
             std::any_of(documented_missing_call_id->begin(), documented_missing_call_id->end(),
                         [](auto const& event) { return event.type == ava::provider::StreamEventType::Error; }),
         "OpenAI documented output items reject a missing logical call_id instead of falling back to item.id");
  auto documented_empty_call_id = ava::provider::parse_openai_sse(
      "data: "
      "{\"type\":\"response.output_item.added\",\"item\":{\"id\":\"fc_empty_call\",\"type\":\"function_call\",\"call_id\":\"\",\"name\":\"read_file\","
      "\"arguments\":\"\"}}\n\n"
      "data: [DONE]\n\n");
  expect(documented_empty_call_id &&
             std::none_of(documented_empty_call_id->begin(), documented_empty_call_id->end(),
                          [](auto const& event) { return event.type == ava::provider::StreamEventType::ToolCallStart; }) &&
             std::any_of(documented_empty_call_id->begin(), documented_empty_call_id->end(),
                         [](auto const& event) { return event.type == ava::provider::StreamEventType::Error; }),
         "OpenAI documented output items reject an explicit empty logical call_id");
  auto documented_missing_name = ava::provider::parse_openai_sse(
      "data: "
      "{\"type\":\"response.output_item.added\",\"item\":{\"id\":\"fc_missing_name\",\"type\":\"function_call\",\"call_id\":\"opaque-missing-name\","
      "\"arguments\":\"\"}}\n\n"
      "data: [DONE]\n\n");
  auto documented_empty_name = ava::provider::parse_openai_sse(
      "data: "
      "{\"type\":\"response.output_item.done\",\"item\":{\"id\":\"fc_empty_name\",\"type\":\"function_call\",\"call_id\":\"opaque-empty-name\","
      "\"name\":\"\","
      "\"arguments\":\"\"}}\n\n"
      "data: [DONE]\n\n");
  expect(documented_missing_name && documented_empty_name &&
             std::none_of(documented_missing_name->begin(), documented_missing_name->end(),
                          [](auto const& event) { return event.type == ava::provider::StreamEventType::ToolCallStart; }) &&
             std::none_of(documented_empty_name->begin(), documented_empty_name->end(),
                          [](auto const& event) { return event.type == ava::provider::StreamEventType::ToolCallStart; }) &&
             std::any_of(documented_missing_name->begin(), documented_missing_name->end(),
                         [](auto const& event) { return event.type == ava::provider::StreamEventType::Error; }) &&
             std::any_of(documented_empty_name->begin(), documented_empty_name->end(),
                         [](auto const& event) { return event.type == ava::provider::StreamEventType::Error; }),
         "OpenAI documented output items require a present nonempty function name");
  auto documented_missing_arguments = ava::provider::parse_openai_sse(
      "data: {\"type\":\"response.output_item.added\",\"item\":{\"id\":\"fc_missing_arguments\",\"type\":\"function_call\","
      "\"call_id\":\"opaque-missing-arguments\",\"name\":\"read_file\"}}\n\n"
      "data: [DONE]\n\n");
  auto documented_wrong_type_arguments = ava::provider::parse_openai_sse(
      "data: {\"type\":\"response.output_item.done\",\"item\":{\"id\":\"fc_wrong_arguments\",\"type\":\"function_call\","
      "\"call_id\":\"opaque-wrong-arguments\",\"name\":\"read_file\",\"arguments\":{}}}\n\n"
      "data: [DONE]\n\n");
  expect(documented_missing_arguments && documented_wrong_type_arguments &&
             std::none_of(documented_missing_arguments->begin(), documented_missing_arguments->end(),
                          [](auto const& event) { return event.type == ava::provider::StreamEventType::ToolCallStart; }) &&
             std::none_of(documented_wrong_type_arguments->begin(), documented_wrong_type_arguments->end(),
                          [](auto const& event) { return event.type == ava::provider::StreamEventType::ToolCallStart; }) &&
             std::any_of(documented_missing_arguments->begin(), documented_missing_arguments->end(),
                         [](auto const& event) { return event.type == ava::provider::StreamEventType::Error; }) &&
             std::any_of(documented_wrong_type_arguments->begin(), documented_wrong_type_arguments->end(),
                         [](auto const& event) { return event.type == ava::provider::StreamEventType::Error; }),
         "OpenAI documented output items require a present string arguments field before dispatch");
  auto orphan_documented_item_id = ava::provider::parse_openai_sse(
      "data: {\"type\":\"response.function_call_arguments.delta\",\"item_id\":\"fc_orphan\",\"delta\":\"{}\"}\n\n"
      "data: [DONE]\n\n");
  auto empty_documented_item_id = ava::provider::parse_openai_sse(
      "data: {\"type\":\"response.function_call_arguments.done\",\"item_id\":\"\",\"arguments\":\"\"}\n\n"
      "data: [DONE]\n\n");
  expect(orphan_documented_item_id && empty_documented_item_id &&
             std::none_of(orphan_documented_item_id->begin(), orphan_documented_item_id->end(),
                          [](auto const& event) { return event.type == ava::provider::StreamEventType::ToolCallStart; }) &&
             std::none_of(empty_documented_item_id->begin(), empty_documented_item_id->end(),
                          [](auto const& event) { return event.type == ava::provider::StreamEventType::ToolCallStart; }) &&
             std::any_of(orphan_documented_item_id->begin(), orphan_documented_item_id->end(),
                         [](auto const& event) { return event.type == ava::provider::StreamEventType::Error; }) &&
             std::any_of(empty_documented_item_id->begin(), empty_documented_item_id->end(),
                         [](auto const& event) { return event.type == ava::provider::StreamEventType::Error; }),
         "OpenAI documented argument events reject orphan and empty item IDs without legacy ID promotion");
  auto documented_mapping_change = ava::provider::parse_openai_sse(
      "data: "
      "{\"type\":\"response.output_item.added\",\"item\":{\"id\":\"fc_mapping\",\"type\":\"function_call\",\"call_id\":\"opaque-first\",\"name\":"
      "\"read_file\","
      "\"arguments\":\"\"}"
      "}\n\n"
      "data: "
      "{\"type\":\"response.output_item.done\",\"item\":{\"id\":\"fc_mapping\",\"type\":\"function_call\",\"call_id\":\"opaque-second\",\"name\":"
      "\"read_file\","
      "\"arguments\":\"\"}"
      "}\n\n"
      "data: [DONE]\n\n");
  expect(documented_mapping_change &&
             std::count_if(documented_mapping_change->begin(), documented_mapping_change->end(),
                           [](auto const& event) { return event.type == ava::provider::StreamEventType::ToolCallStart; }) == 1 &&
             std::any_of(documented_mapping_change->begin(), documented_mapping_change->end(),
                         [](auto const& event) { return event.type == ava::provider::StreamEventType::Error; }),
         "OpenAI documented item-id mappings reject a changed logical call_id without dispatching a replacement call");
  auto documented_name_change = ava::provider::parse_openai_sse(
      "data: "
      "{\"type\":\"response.output_item.added\",\"item\":{\"id\":\"fc_name\",\"type\":\"function_call\",\"call_id\":\"opaque-name\",\"name\":\"read_"
      "file\","
      "\"arguments\":\"\"}}"
      "\n\n"
      "data: "
      "{\"type\":\"response.output_item.done\",\"item\":{\"id\":\"fc_name\",\"type\":\"function_call\",\"call_id\":\"opaque-name\",\"name\":\"write_"
      "file\","
      "\"arguments\":\"\"}}"
      "\n\n"
      "data: [DONE]\n\n");
  expect(documented_name_change && std::any_of(documented_name_change->begin(), documented_name_change->end(),
                                               [](auto const& event) { return event.type == ava::provider::StreamEventType::Error; }),
         "OpenAI documented item-id mappings reject a changed function name");
  auto documented_mapping_collision = ava::provider::parse_openai_sse(
      "data: "
      "{\"type\":\"response.output_item.added\",\"item\":{\"id\":\"fc_first\",\"type\":\"function_call\",\"call_id\":\"opaque-collision\",\"name\":"
      "\"read_"
      "file\",\"arguments\":\"\"}}\n\n"
      "data: "
      "{\"type\":\"response.output_item.added\",\"item\":{\"id\":\"fc_second\",\"type\":\"function_call\",\"call_id\":\"opaque-collision\",\"name\":"
      "\"read_"
      "file\",\"arguments\":\"\"}}\n\n"
      "data: [DONE]\n\n");
  expect(documented_mapping_collision &&
             std::count_if(documented_mapping_collision->begin(), documented_mapping_collision->end(),
                           [](auto const& event) { return event.type == ava::provider::StreamEventType::ToolCallStart; }) == 1 &&
             std::any_of(documented_mapping_collision->begin(), documented_mapping_collision->end(),
                         [](auto const& event) { return event.type == ava::provider::StreamEventType::Error; }),
         "OpenAI documented item-id mappings reject logical call-id collisions before dispatching a second call");

  auto const reasoning_tool_item_json =
      R"({"id":"rs_private","type":"reasoning","summary":[{"type":"summary_text","text":"check plan"}],"status":"completed","encrypted_content":"opaque-ciphertext"})";
  auto reasoning_tool = ava::provider::parse_openai_sse(
      std::string("data: {\"type\":\"response.output_item.added\",\"item\":{\"id\":\"rs_private\",\"type\":\"reasoning\"}}\n\n") +
      "data: {\"type\":\"response.output_item.done\",\"item\":" + reasoning_tool_item_json +
      "}\n\n"
      "data: "
      "{\"type\":\"response.output_item.added\",\"item\":{\"id\":\"fc_private\",\"type\":\"function_call\",\"call_id\":\"call_private\",\"name\":"
      "\"read_file\","
      "\"arguments\":\"\"}"
      "}\n\n"
      "data: {\"type\":\"response.function_call_arguments.done\",\"item_id\":\"fc_private\",\"arguments\":\"{\\\"path\\\":\\\"note.txt\\\"}\"}\n\n"
      "data: "
      "{\"type\":\"response.output_item.done\",\"item\":{\"id\":\"fc_private\",\"type\":\"function_call\",\"call_id\":\"call_private\",\"name\":\"read_"
      "file\","
      "\"arguments\":\"{\\\"path\\\":\\\"note.txt\\\"}\"}}\n\n"
      "data: [DONE]\n\n");
  auto const private_reasoning_end = reasoning_tool ? std::find_if(reasoning_tool->begin(), reasoning_tool->end(),
                                                                   [](auto const& event) { return event.type == ava::provider::StreamEventType::ReasoningEnd; })
                                                    : std::vector<ava::provider::StreamEvent>::const_iterator{};
  expect(reasoning_tool && private_reasoning_end != reasoning_tool->end() && private_reasoning_end->reasoning_native_item_json == reasoning_tool_item_json,
         "OpenAI Responses stream retains the exact private completed reasoning item for native tool continuation");
  auto malformed_stream_reasoning = ava::provider::parse_openai_sse(
      "data: {\"type\":\"response.output_item.done\",\"item\":{\"id\":\"rs_missing_summary\",\"type\":\"reasoning\",\"text\":\"safe summary\"}}\n\n"
      "data: [DONE]\n\n");
  auto const malformed_stream_reasoning_end = malformed_stream_reasoning
                                                  ? std::find_if(malformed_stream_reasoning->begin(), malformed_stream_reasoning->end(),
                                                                 [](auto const& event) { return event.type == ava::provider::StreamEventType::ReasoningEnd; })
                                                  : std::vector<ava::provider::StreamEvent>::const_iterator{};
  expect(malformed_stream_reasoning && malformed_stream_reasoning_end != malformed_stream_reasoning->end() &&
             malformed_stream_reasoning_end->reasoning_native_item_json.empty(),
         "OpenAI streaming parser keeps readable reasoning while dropping malformed native replay metadata");
  auto malformed_summary_shape = ava::provider::parse_openai_sse(
      "data: {\"type\":\"response.output_item.done\",\"item\":{\"id\":\"rs_scalar_summary\",\"type\":\"reasoning\","
      "\"summary\":[\"not-an-object\"]}}\n\n"
      "data: [DONE]\n\n");
  expect(malformed_summary_shape && malformed_summary_shape->size() == 1 && (*malformed_summary_shape)[0].type == ava::provider::StreamEventType::Error &&
             (*malformed_summary_shape)[0].error_message == "OpenAI response parser limit exceeded",
         "OpenAI streaming parser rejects malformed reasoning summary arrays with one fixed terminal parser error");

  auto arguments_done_only = ava::provider::parse_openai_sse(
      "data: "
      "{\"type\":\"response.output_item.added\",\"item\":{\"id\":\"fc_done\",\"type\":\"function_call\",\"call_id\":\"call_done\",\"name\":\"read_"
      "file\","
      "\"arguments\":\"\"}}\n\n"
      "data: {\"type\":\"response.function_call_arguments.done\",\"call_id\":\"call_done\",\"arguments\":\"{\\\"path\\\":\\\"done.txt\\\"}\"}\n\n"
      "data: "
      "{\"type\":\"response.output_item.done\",\"item\":{\"id\":\"fc_done\",\"type\":\"function_call\",\"call_id\":\"call_done\",\"name\":\"read_"
      "file\","
      "\"arguments\":\"{\\\"path\\\":\\\"done.txt\\\"}\"}}\n\n"
      "data: [DONE]\n\n");
  expect(arguments_done_only && arguments_done_only->size() == 4 && (*arguments_done_only)[1].type == ava::provider::StreamEventType::ToolCallDelta &&
             (*arguments_done_only)[1].text == R"({"path":"done.txt"})",
         "OpenAI function_call_arguments.done supplies complete arguments when no deltas arrived");

  auto arguments_item_done_only = ava::provider::parse_openai_sse(
      "data: "
      "{\"type\":\"response.output_item.done\",\"item\":{\"id\":\"fc_final\",\"type\":\"function_call\",\"call_id\":\"call_final\",\"name\":\"read_"
      "file\","
      "\"arguments\":\"{\\\"path\\\":\\\"final.txt\\\"}\"}}\n\n"
      "data: [DONE]\n\n");
  expect(arguments_item_done_only && arguments_item_done_only->size() == 4 &&
             (*arguments_item_done_only)[0].type == ava::provider::StreamEventType::ToolCallStart && (*arguments_item_done_only)[0].tool_name == "read_file" &&
             (*arguments_item_done_only)[1].text == R"({"path":"final.txt"})" &&
             (*arguments_item_done_only)[2].type == ava::provider::StreamEventType::ToolCallEnd,
         "OpenAI output_item.done creates a named complete function call when added and deltas were absent");

  auto identical_final_arguments = ava::provider::parse_openai_sse(
      "data: "
      "{\"type\":\"response.output_item.added\",\"item\":{\"id\":\"fc_equal\",\"type\":\"function_call\",\"call_id\":\"call_equal\",\"name\":\"read_"
      "file\","
      "\"arguments\":\"{}\"}}\n\n"
      "data: {\"type\":\"response.function_call_arguments.done\",\"item_id\":\"fc_equal\",\"arguments\":\"{}\"}\n\n"
      "data: "
      "{\"type\":\"response.output_item.done\",\"item\":{\"id\":\"fc_equal\",\"type\":\"function_call\",\"call_id\":\"call_equal\",\"name\":\"read_"
      "file\","
      "\"arguments\":\"{}\"}}\n\n"
      "data: [DONE]\n\n");
  expect(identical_final_arguments && std::count_if(identical_final_arguments->begin(), identical_final_arguments->end(),
                                                    [](auto const& event) { return event.type == ava::provider::StreamEventType::ToolCallDelta; }) == 1,
         "OpenAI duplicate complete function arguments are emitted once");

  auto suffix_final_arguments = ava::provider::parse_openai_sse(
      "data: "
      "{\"type\":\"response.output_item.added\",\"item\":{\"id\":\"fc_suffix\",\"type\":\"function_call\",\"call_id\":\"call_suffix\",\"name\":\"read_"
      "file\","
      "\"arguments\":\"{\"}}\n\n"
      "data: {\"type\":\"response.function_call_arguments.done\",\"item_id\":\"fc_suffix\",\"arguments\":\"{}\"}\n\n"
      "data: "
      "{\"type\":\"response.output_item.done\",\"item\":{\"id\":\"fc_suffix\",\"type\":\"function_call\",\"call_id\":\"call_suffix\",\"name\":\"read_"
      "file\","
      "\"arguments\":\"{}\"}}"
      "\n\n"
      "data: [DONE]\n\n");
  expect(suffix_final_arguments && suffix_final_arguments->size() == 5 && (*suffix_final_arguments)[1].text == "{" && (*suffix_final_arguments)[2].text == "}",
         "OpenAI complete function arguments append only their missing suffix");

  auto conflicting_final_arguments = ava::provider::parse_openai_sse(
      "data: "
      "{\"type\":\"response.output_item.added\",\"item\":{\"id\":\"fc_conflict\",\"type\":\"function_call\",\"call_id\":\"call_conflict\",\"name\":"
      "\"read_"
      "file\",\"arguments\":\"{}\"}}\n\n"
      "data: "
      "{\"type\":\"response.function_call_arguments.done\",\"item_id\":\"fc_conflict\",\"arguments\":\"{\\\"path\\\":\\\"conflict.txt\\\"}\"}\n\n");
  expect(conflicting_final_arguments && std::any_of(conflicting_final_arguments->begin(), conflicting_final_arguments->end(),
                                                    [](auto const& event) { return event.type == ava::provider::StreamEventType::Error; }),
         "OpenAI conflicting complete function arguments fail the provider stream closed");

  ava::provider::OpenAIStreamParser reusable_parser;
  auto first_reusable = reusable_parser.append(
      "data: "
      "{\"type\":\"response.output_item.done\",\"item\":{\"id\":\"fc_reset_one\",\"type\":\"function_call\",\"call_id\":\"call_reset_one\",\"name\":"
      "\"read_"
      "file\",\"arguments\":\"{}\"}}\n\n"
      "data: [DONE]\n\n");
  auto first_reusable_finish = reusable_parser.finish();
  auto second_reusable = reusable_parser.append(
      "data: "
      "{\"type\":\"response.output_item.done\",\"item\":{\"id\":\"fc_reset_two\",\"type\":\"function_call\",\"call_id\":\"call_reset_two\",\"name\":"
      "\"read_"
      "file\",\"arguments\":\"{}\"}}\n\n"
      "data: [DONE]\n\n");
  auto second_reusable_finish = reusable_parser.finish();
  expect(first_reusable && first_reusable_finish && second_reusable && second_reusable_finish && second_reusable->size() == 4 &&
             (*second_reusable)[0].tool_call_id == "call_reset_two",
         "OpenAI stream parser clears function-call argument state after finish");

  ava::provider::StreamEvent observed_private_reasoning{.type = ava::provider::StreamEventType::ReasoningEnd,
                                                        .text = "",
                                                        .tool_call_id = "",
                                                        .tool_name = "",
                                                        .error_message = "",
                                                        .usage = std::nullopt,
                                                        .provider_item_id = "rs_private",
                                                        .provider_output_index = 7,
                                                        .assistant_phase = ava::provider::AssistantPhase::Commentary,
                                                        .finish_reason = std::nullopt,
                                                        .reasoning_format = "",
                                                        .reasoning_signature = "",
                                                        .reasoning_redacted_data = "",
                                                        .reasoning_native_item_json = reasoning_tool_item_json,
                                                        .redacted = false,
                                                        .reasoning_signature_present = false};
  ava::provider::StreamEvent public_private_reasoning;
  ava::agent::AgentLoopOptions bridge_options;
  bridge_options.on_stream_event = [&public_private_reasoning](auto const& event) -> ava::core::VoidResult {
    public_private_reasoning = event;
    return {};
  };
  auto bridged_private_reasoning = ava::agent::publish_stream_event(bridge_options, observed_private_reasoning);
  expect(bridged_private_reasoning && public_private_reasoning.reasoning_native_item_json.empty() && public_private_reasoning.provider_item_id.empty() &&
             !public_private_reasoning.provider_output_index && public_private_reasoning.assistant_phase == ava::provider::AssistantPhase::Unknown,
         "public stream bridge redacts private OpenAI reasoning, output-item metadata, and assistant phase");

  auto reasoning_summary = ava::provider::parse_openai_sse(
      "data: {\"type\":\"response.output_item.added\",\"item\":{\"id\":\"rs_1\",\"type\":\"reasoning\"}}\n\n"
      "data: {\"type\":\"response.reasoning_summary_part.added\",\"item_id\":\"rs_1\"}\n\n"
      "data: {\"type\":\"response.reasoning_summary_text.delta\",\"delta\":\"plan\"}\n\n"
      "data: {\"type\":\"response.reasoning_summary_text.done\",\"text\":\"plan\"}\n\n"
      "data: {\"type\":\"response.output_text.delta\",\"delta\":\"answer\"}\n\n"
      "data: [DONE]\n\n");
  expect(reasoning_summary && reasoning_summary->size() == 5 && (*reasoning_summary)[0].type == ava::provider::StreamEventType::ReasoningStart &&
             (*reasoning_summary)[0].reasoning_format == "openai_responses" && (*reasoning_summary)[1].type == ava::provider::StreamEventType::ReasoningDelta &&
             (*reasoning_summary)[1].text == "plan" && (*reasoning_summary)[2].type == ava::provider::StreamEventType::ReasoningEnd &&
             (*reasoning_summary)[3].type == ava::provider::StreamEventType::TextDelta && (*reasoning_summary)[3].text == "answer" &&
             (*reasoning_summary)[4].type == ava::provider::StreamEventType::Done,
         "OpenAI Responses SSE emits reasoning summary before answer text");
  auto reasoning_duplicate_done = ava::provider::parse_openai_sse(
      "data: {\"type\":\"response.output_item.added\",\"item\":{\"id\":\"rs_1\",\"type\":\"reasoning\"}}\n\n"
      "data: {\"type\":\"response.reasoning_summary_text.delta\",\"item_id\":\"rs_1\",\"delta\":\"plan\"}\n\n"
      "data: {\"type\":\"response.reasoning_summary_text.done\",\"item_id\":\"rs_1\",\"text\":\"plan\"}\n\n"
      "data: {\"type\":\"response.reasoning_summary_part.done\",\"item_id\":\"rs_1\",\"text\":\"plan\"}\n\n"
      "data: {\"type\":\"response.output_item.done\",\"item\":{\"id\":\"rs_1\",\"type\":\"reasoning\","
      "\"summary\":[{\"type\":\"summary_text\",\"text\":\"plan\"}]}}\n\n"
      "data: {\"type\":\"response.output_text.delta\",\"delta\":\"answer\"}\n\n"
      "data: [DONE]\n\n");
  expect(reasoning_duplicate_done && reasoning_duplicate_done->size() == 5 &&
             (*reasoning_duplicate_done)[0].type == ava::provider::StreamEventType::ReasoningStart &&
             (*reasoning_duplicate_done)[1].type == ava::provider::StreamEventType::ReasoningDelta && (*reasoning_duplicate_done)[1].text == "plan" &&
             (*reasoning_duplicate_done)[2].type == ava::provider::StreamEventType::ReasoningEnd &&
             (*reasoning_duplicate_done)[3].type == ava::provider::StreamEventType::TextDelta &&
             (*reasoning_duplicate_done)[4].type == ava::provider::StreamEventType::Done,
         "OpenAI Responses SSE ignores duplicate reasoning summary done events for one item");
  auto reasoning_done_only = ava::provider::parse_openai_sse(
      "data: {\"type\":\"response.output_item.added\",\"item\":{\"id\":\"rs_2\",\"type\":\"reasoning\"}}\n\n"
      "data: {\"type\":\"response.reasoning_summary_text.done\",\"text\":\"done-only plan\"}\n\n"
      "data: {\"type\":\"response.output_text.delta\",\"delta\":\"answer\"}\n\n"
      "data: [DONE]\n\n");
  expect(reasoning_done_only && reasoning_done_only->size() == 5 && (*reasoning_done_only)[0].type == ava::provider::StreamEventType::ReasoningStart &&
             (*reasoning_done_only)[1].type == ava::provider::StreamEventType::ReasoningDelta && (*reasoning_done_only)[1].text == "done-only plan" &&
             (*reasoning_done_only)[2].type == ava::provider::StreamEventType::ReasoningEnd &&
             (*reasoning_done_only)[3].type == ava::provider::StreamEventType::TextDelta &&
             (*reasoning_done_only)[4].type == ava::provider::StreamEventType::Done,
         "OpenAI Responses SSE preserves done-only reasoning summary text");
  auto reasoning_delta_text = [](std::vector<ava::provider::StreamEvent> const& events) {
    std::string text;
    std::size_t deltas = 0;
    for (auto const& event : events)
    {
      if (event.type == ava::provider::StreamEventType::ReasoningDelta)
      {
        text += event.text;
        ++deltas;
      }
    }
    return std::pair{std::move(text), deltas};
  };
  auto const reasoning_partial_prefix = ava::provider::parse_openai_sse(
      "data: {\"type\":\"response.output_item.added\",\"item\":{\"id\":\"rs_prefix\",\"type\":\"reasoning\"}}\n\n"
      "data: {\"type\":\"response.reasoning_summary_text.delta\",\"item_id\":\"rs_prefix\",\"delta\":\"pre\"}\n\n"
      "data: {\"type\":\"response.reasoning_summary_text.done\",\"item_id\":\"rs_prefix\",\"text\":\"prefix\"}\n\n"
      "data: "
      "{\"type\":\"response.output_item.done\",\"item\":{\"id\":\"rs_prefix\",\"type\":\"reasoning\",\"summary\":[{\"type\":\"summary_text\",\"text\":"
      "\"prefix\"}]}}\n\n"
      "data: [DONE]\n\n");
  auto const reasoning_partial_prefix_non_stream = provider.parse_response(
      ava::http::HttpResponse{.status_code = 200,
                              .headers = {},
                              .body = "{\"status\":\"completed\",\"output\":[{\"id\":\"rs_prefix\",\"type\":\"reasoning\",\"summary\":[{\"type\":\"summary_"
                                      "text\",\"text\":\"prefix\"}]}]}"},
      false);
  auto const reasoning_duplicate_complete = ava::provider::parse_openai_sse(
      "data: {\"type\":\"response.output_item.added\",\"item\":{\"id\":\"rs_duplicate\",\"type\":\"reasoning\"}}\n\n"
      "data: {\"type\":\"response.reasoning_text.delta\",\"item_id\":\"rs_duplicate\",\"delta\":\"same\"}\n\n"
      "data: {\"type\":\"response.reasoning_text.done\",\"item_id\":\"rs_duplicate\",\"text\":\"same\"}\n\n"
      "data: "
      "{\"type\":\"response.output_item.done\",\"item\":{\"id\":\"rs_duplicate\",\"type\":\"reasoning\",\"summary\":[{\"type\":\"summary_text\",\"text\":"
      "\"same\"}]}}\n\n"
      "data: [DONE]\n\n");
  auto const reasoning_conflict = ava::provider::parse_openai_sse(
      "data: {\"type\":\"response.output_item.added\",\"item\":{\"id\":\"rs_conflict\",\"type\":\"reasoning\"}}\n\n"
      "data: {\"type\":\"response.reasoning_text.delta\",\"item_id\":\"rs_conflict\",\"delta\":\"pre\"}\n\n"
      "data: {\"type\":\"response.reasoning_text.done\",\"item_id\":\"rs_conflict\",\"text\":\"other\"}\n\n");
  bool const partial_prefix_parity =
      reasoning_partial_prefix && reasoning_partial_prefix_non_stream && reasoning_delta_text(*reasoning_partial_prefix).first == "prefix" &&
      reasoning_delta_text(*reasoning_partial_prefix_non_stream).first == "prefix" && reasoning_delta_text(*reasoning_partial_prefix).second == 2;
  bool const duplicate_complete_noop = reasoning_duplicate_complete && reasoning_delta_text(*reasoning_duplicate_complete).first == "same" &&
                                       reasoning_delta_text(*reasoning_duplicate_complete).second == 1;
  bool const conflict_rejected = reasoning_conflict && std::ranges::any_of(*reasoning_conflict, [](auto const& event) {
                                   return event.type == ava::provider::StreamEventType::Error && event.error_message == "conflicting OpenAI reasoning text";
                                 });
  expect(partial_prefix_parity && duplicate_complete_noop && conflict_rejected,
         "OpenAI reasoning done/output-item completions reconcile suffixes, ignore duplicates, and reject conflicts like non-stream output");
}

}  // namespace ava::tests::provider_openai_suite
