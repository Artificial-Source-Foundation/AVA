#include <string>
#include <vector>

#include "ava/provider/openai_stream_reasoning.h"
#include "ava/provider/provider.h"
#include "tests/support/test_harness.h"

namespace {

void test_memory_helpers_deduplicate_non_empty_values()
{
  std::vector<std::string> values;
  ava::provider::detail::remember_openai_stream_value(values, "rs_1");
  ava::provider::detail::remember_openai_stream_value(values, "");
  ava::provider::detail::remember_openai_stream_value(values, "rs_1");
  ava::provider::detail::remember_openai_stream_value(values, "rs_2");

  expect(values.size() == 2 && values[0] == "rs_1" && values[1] == "rs_2",
         "OpenAI stream reasoning memory deduplicates non-empty values");
  expect(ava::provider::detail::openai_stream_remembers(values, "rs_1") &&
             !ava::provider::detail::openai_stream_remembers(values, "rs_3"),
         "OpenAI stream reasoning memory reports remembered values");
}

void test_reasoning_item_id_extraction()
{
  auto const direct = ava::provider::detail::openai_reasoning_item_id_from_event(R"({"item_id":"rs_direct"})");
  auto const output = ava::provider::detail::openai_reasoning_item_id_from_event(R"({"output_item_id":"rs_output"})");
  auto const nested = ava::provider::detail::openai_reasoning_item_id_from_event(
      R"({"type":"response.output_item.added"})", R"({"id":"rs_nested","type":"reasoning"})");

  expect(direct == "rs_direct" && output == "rs_output" && nested == "rs_nested",
         "OpenAI stream reasoning extracts item ids from direct and nested event fields");
}

void test_reasoning_start_delta_end_sequence()
{
  ava::provider::detail::OpenAIStreamEventState state;
  std::vector<ava::provider::StreamEvent> events;

  ava::provider::detail::set_active_openai_reasoning_item_id(state, "rs_1");
  ava::provider::detail::append_openai_reasoning_start_if_needed(events, state);
  ava::provider::detail::append_openai_reasoning_start_if_needed(events, state);
  ava::provider::detail::append_openai_reasoning_delta(events, state, "think");
  ava::provider::detail::append_openai_reasoning_end_if_open(events, state);
  ava::provider::detail::append_openai_reasoning_end_if_open(events, state);

  expect(events.size() == 3 && events[0].type == ava::provider::StreamEventType::ReasoningStart &&
             events[1].type == ava::provider::StreamEventType::ReasoningDelta && events[1].text == "think" &&
             events[2].type == ava::provider::StreamEventType::ReasoningEnd,
         "OpenAI stream reasoning emits one start, delta, and end sequence");
  expect(!state.reasoning_open && !state.reasoning_text_seen && state.active_reasoning_item_id.empty() &&
             state.active_reasoning_text.empty(),
         "OpenAI stream reasoning clears active state after end");
  expect(state.completed_reasoning_item_ids.size() == 1 && state.completed_reasoning_item_ids[0] == "rs_1" &&
             state.completed_reasoning_texts.size() == 1 && state.completed_reasoning_texts[0] == "think",
         "OpenAI stream reasoning remembers completed ids and text");
}

void test_empty_delta_opens_reasoning_without_text()
{
  ava::provider::detail::OpenAIStreamEventState state;
  std::vector<ava::provider::StreamEvent> events;

  ava::provider::detail::append_openai_reasoning_delta(events, state, "");
  expect(events.size() == 1 && events[0].type == ava::provider::StreamEventType::ReasoningStart &&
             state.reasoning_open && !state.reasoning_text_seen && state.active_reasoning_text.empty(),
         "OpenAI stream reasoning empty deltas open reasoning but do not emit text deltas");
}

}  // namespace

void run_provider_openai_stream_reasoning_tests()
{
  test_memory_helpers_deduplicate_non_empty_values();
  test_reasoning_item_id_extraction();
  test_reasoning_start_delta_end_sequence();
  test_empty_delta_opens_reasoning_without_text();
}
