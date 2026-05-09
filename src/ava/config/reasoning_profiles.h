#pragma once

#include "ava/config/model_config.h"

#include <string>
#include <vector>

namespace ava::config {

struct ReasoningProfile
{
  std::string api_family;
  std::string format;
  std::vector<std::string> levels;
  std::string request_parameters;
};

[[nodiscard]] ReasoningProfile const& openai_responses_reasoning_profile();
[[nodiscard]] ReasoningProfile const& openai_compatible_reasoning_content_profile();
[[nodiscard]] ReasoningProfile const& anthropic_thinking_reasoning_profile();

[[nodiscard]] std::string reasoning_parameter_text(ModelInfo const& model);

}  // namespace ava::config
