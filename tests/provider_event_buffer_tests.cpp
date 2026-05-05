#include <string>
#include <utility>
#include <vector>

#include "ava/agent/provider_event_buffer.h"
#include "ava/core/error.h"
#include "ava/provider/provider.h"
#include "tests/support/test_harness.h"

namespace {

ava::provider::StreamEvent text_delta(std::string text)
{
  ava::provider::StreamEvent event;
  event.type = ava::provider::StreamEventType::TextDelta;
  event.text = std::move(text);
  return event;
}

ava::provider::StreamEvent reasoning_event(ava::provider::StreamEventType type, std::string text = {})
{
  ava::provider::StreamEvent event;
  event.type = type;
  event.text = std::move(text);
  event.reasoning_format = "test_reasoning";
  return event;
}

ava::provider::StreamEvent tool_event(ava::provider::StreamEventType type, std::string call_id, std::string text = {})
{
  ava::provider::StreamEvent event;
  event.type = type;
  event.tool_call_id = std::move(call_id);
  event.tool_name = "read_file";
  event.text = std::move(text);
  return event;
}

ava::agent::ProviderEventPublisher capture_publisher(std::vector<ava::provider::StreamEvent>& events)
{
  return [&events](ava::provider::StreamEvent const& event) -> ava::core::VoidResult {
    events.push_back(event);
    return {};
  };
}

void test_provider_event_buffer_appends_and_publishes_events()
{
  ava::agent::ProviderEventBuffer buffer(ava::agent::ProviderOutputLimits{});
  std::vector<ava::provider::StreamEvent> published;
  auto appended =
      buffer.append({text_delta("hello"), tool_event(ava::provider::StreamEventType::ToolCallStart, "call_1"),
                     tool_event(ava::provider::StreamEventType::ToolCallDelta, "call_1", "{\"path\":")},
                    capture_publisher(published));

  expect(appended.has_value(), "provider event buffer accepts valid text and tool events");
  expect(buffer.events().size() == 3 && published.size() == 3, "provider event buffer stores and publishes all events");
  expect(buffer.events()[0].text == "hello" && buffer.events()[2].text == "{\"path\":",
         "provider event buffer preserves provider event payloads");
}

void test_provider_event_buffer_reasoning_publishes_when_non_stream_events_are_suppressed()
{
  ava::agent::ProviderEventBuffer buffer(ava::agent::ProviderOutputLimits{});
  std::vector<ava::provider::StreamEvent> published;
  auto appended =
      buffer.append({text_delta("hidden until final"), reasoning_event(ava::provider::StreamEventType::ReasoningStart),
                     reasoning_event(ava::provider::StreamEventType::ReasoningDelta, "thinking"),
                     reasoning_event(ava::provider::StreamEventType::ReasoningEnd)},
                    capture_publisher(published), false);

  expect(appended.has_value(), "provider event buffer accepts mixed text and reasoning events");
  expect(buffer.events().size() == 4, "provider event buffer stores non-stream provider events for turn parsing");
  expect(published.size() == 3 && published[0].type == ava::provider::StreamEventType::ReasoningStart &&
             published[1].type == ava::provider::StreamEventType::ReasoningDelta &&
             published[2].type == ava::provider::StreamEventType::ReasoningEnd,
         "provider event buffer publishes reasoning events even when text deltas are suppressed");
}

void test_provider_event_buffer_enforces_output_limits()
{
  ava::agent::ProviderEventBuffer text_buffer(
      ava::agent::ProviderOutputLimits{.max_events = 0, .max_assistant_text_bytes = 4, .max_tool_argument_bytes = 0});
  auto text_ok = text_buffer.append({text_delta("1234")}, {});
  auto text_too_large = text_buffer.append({text_delta("5")}, {});
  expect(text_ok.has_value() && !text_too_large.has_value(),
         "provider event buffer enforces accumulated assistant text byte limits");

  ava::agent::ProviderEventBuffer tool_buffer(
      ava::agent::ProviderOutputLimits{.max_events = 0, .max_assistant_text_bytes = 0, .max_tool_argument_bytes = 3});
  auto tool_ok = tool_buffer.append({tool_event(ava::provider::StreamEventType::ToolCallStart, "call_1"),
                                     tool_event(ava::provider::StreamEventType::ToolCallDelta, "call_1", "abc")},
                                    {});
  auto tool_too_large =
      tool_buffer.append({tool_event(ava::provider::StreamEventType::ToolCallDelta, "call_1", "d")}, {});
  expect(tool_ok.has_value() && !tool_too_large.has_value(),
         "provider event buffer enforces accumulated tool argument byte limits per call");

  ava::agent::ProviderEventBuffer count_buffer(
      ava::agent::ProviderOutputLimits{.max_events = 1, .max_assistant_text_bytes = 0, .max_tool_argument_bytes = 0});
  auto count_ok = count_buffer.append({text_delta("one")}, {});
  auto count_too_large = count_buffer.append({text_delta("two")}, {});
  expect(count_ok.has_value() && !count_too_large.has_value(),
         "provider event buffer enforces provider event count limits");
}

void test_provider_event_buffer_rejects_invalid_tool_ids()
{
  ava::agent::ProviderEventBuffer buffer(ava::agent::ProviderOutputLimits{});
  auto empty_id = buffer.append({tool_event(ava::provider::StreamEventType::ToolCallStart, "")}, {});
  expect(!empty_id.has_value() && buffer.empty(), "provider event buffer rejects empty provider tool call IDs");
}

void test_provider_event_buffer_publisher_failure_is_not_stored()
{
  ava::agent::ProviderEventBuffer buffer(ava::agent::ProviderOutputLimits{});
  auto failing_publisher = [](ava::provider::StreamEvent const&) -> ava::core::VoidResult {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Provider, "publisher failed"));
  };

  auto published = buffer.append({text_delta("not stored")}, failing_publisher);
  expect(!published.has_value() && buffer.empty(),
         "provider event buffer leaves the buffer unchanged when publishing fails");
}

}  // namespace

void run_provider_event_buffer_tests()
{
  test_provider_event_buffer_appends_and_publishes_events();
  test_provider_event_buffer_reasoning_publishes_when_non_stream_events_are_suppressed();
  test_provider_event_buffer_enforces_output_limits();
  test_provider_event_buffer_rejects_invalid_tool_ids();
  test_provider_event_buffer_publisher_failure_is_not_stored();
}
