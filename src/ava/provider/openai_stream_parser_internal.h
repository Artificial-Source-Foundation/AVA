#pragma once

#include "ava/provider/provider.h"
#include "ava/core/result.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ava::provider::openai_stream_parser_internal {

void append_stream_error(std::vector<StreamEvent>& events, bool& error_seen, std::string message);
ava::core::Result<std::optional<std::size_t>> documented_output_index(std::string_view data, std::string_view item);
ava::core::Result<AssistantPhase> documented_message_phase(std::string_view data, std::string_view item);

}  // namespace ava::provider::openai_stream_parser_internal
