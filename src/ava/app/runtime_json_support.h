#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "ava/agent/mode.h"
#include "ava/app/runtime.h"
#include "ava/config/model_config.h"
#include "ava/config/prompt_config.h"

namespace ava::app::runtime::detail {

[[nodiscard]] int hex_value(char ch);
[[nodiscard]] std::optional<unsigned int> parse_hex_code_unit(std::string_view text, std::size_t hex_start);
[[nodiscard]] bool is_high_surrogate(unsigned int code_unit);
[[nodiscard]] bool is_low_surrogate(unsigned int code_unit);
void append_utf8(std::string& output, unsigned int codepoint);
void append_json_escaped_char(std::string& output, std::string_view object, std::size_t& index);

[[nodiscard]] std::string session_start_data_json(ava::agent::Mode mode, ava::config::ModelInfo const& model,
                                                  ava::config::PromptSelection const& prompt,
                                                  std::size_t context_source_count);
[[nodiscard]] std::string model_change_data_json(ava::config::ModelInfo const& previous,
                                                 ava::config::ModelInfo const& current);
[[nodiscard]] std::string reasoning_change_data_json(ava::config::ModelInfo const& model,
                                                     std::optional<RuntimeReasoningSelection> const& selection);

}  // namespace ava::app::runtime::detail
