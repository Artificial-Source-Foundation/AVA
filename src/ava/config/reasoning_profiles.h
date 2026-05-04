#pragma once

#include <string>
#include <vector>

#include "ava/config/model_config.h"

namespace ava::config {

struct ReasoningProfile {
  std::string api_family;
  std::string format;
  std::vector<std::string> levels;
  std::string request_parameters;
};

[[nodiscard]] const ReasoningProfile& openai_responses_reasoning_profile();
[[nodiscard]] const ReasoningProfile& openai_compatible_reasoning_content_profile();
[[nodiscard]] const ReasoningProfile& anthropic_thinking_reasoning_profile();

[[nodiscard]] std::string reasoning_parameter_text(const ModelInfo& model);

}  // namespace ava::config
