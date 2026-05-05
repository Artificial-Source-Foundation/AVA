#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ava/provider/openai_stream_events.h"
#include "ava/provider/provider.h"

namespace ava::provider::detail {

[[nodiscard]] bool openai_stream_remembers(std::vector<std::string> const& values, std::string_view value);
void remember_openai_stream_value(std::vector<std::string>& values, std::string value);
[[nodiscard]] std::string openai_reasoning_item_id_from_event(std::string_view data,
                                                              std::optional<std::string_view> item = std::nullopt);
void set_active_openai_reasoning_item_id(OpenAIStreamEventState& state, std::string_view item_id);
void append_openai_reasoning_start_if_needed(std::vector<StreamEvent>& events, OpenAIStreamEventState& state);
void append_openai_reasoning_delta(std::vector<StreamEvent>& events, OpenAIStreamEventState& state,
                                   std::string_view text);
void append_openai_reasoning_end_if_open(std::vector<StreamEvent>& events, OpenAIStreamEventState& state);

}  // namespace ava::provider::detail
