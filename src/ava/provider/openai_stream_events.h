#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "ava/provider/provider.h"

namespace ava::provider::detail {

struct OpenAIStreamEventState {
  std::string data;
  bool saw_content = false;
  bool reasoning_open = false;
  bool reasoning_text_seen = false;
  std::string active_reasoning_item_id;
  std::string active_reasoning_text;
  std::vector<std::string> completed_reasoning_item_ids;
  std::vector<std::string> completed_reasoning_texts;
  bool done_seen = false;
  bool error_seen = false;

  void reset();
};

[[nodiscard]] bool is_ignored_openai_lifecycle_event(std::string_view type);
void append_openai_event_for_data(std::vector<StreamEvent>& events, OpenAIStreamEventState& state,
                                  std::string_view data);
void append_openai_events_for_sse_line(std::vector<StreamEvent>& events, OpenAIStreamEventState& state,
                                       std::string line);
void finish_openai_stream_events(std::vector<StreamEvent>& events, OpenAIStreamEventState& state);

}  // namespace ava::provider::detail
