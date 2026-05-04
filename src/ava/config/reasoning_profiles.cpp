#include "ava/config/reasoning_profiles.h"

#include "ava/config/provider_profiles.h"

namespace ava::config {

ReasoningProfile const& openai_responses_reasoning_profile() {
  static ReasoningProfile const profile{.api_family = "openai_responses",
                                        .format = "openai_responses",
                                        .levels = {"low", "medium", "high", "xhigh"},
                                        .request_parameters =
                                            "request.reasoning.effort=<level>; "
                                            "request.reasoning.summary=auto"};
  return profile;
}

ReasoningProfile const& openai_compatible_reasoning_content_profile() {
  static ReasoningProfile const profile{.api_family = "openai_chat_completions",
                                        .format = "reasoning_content",
                                        .levels = {"enabled"},
                                        .request_parameters = "request.thinking.type=<level>"};
  return profile;
}

ReasoningProfile const& anthropic_thinking_reasoning_profile() {
  static ReasoningProfile const profile{
      .api_family = "anthropic_messages",
      .format = "anthropic_thinking",
      .levels = {"enabled", "adaptive"},
      .request_parameters = "request.thinking.type=<level>; enabled requires request.thinking.budget_tokens"};
  return profile;
}

std::string reasoning_parameter_text(ModelInfo const& model) {
  if (!model.supports_reasoning.value_or(false)) return {};

  if (auto provider = reasoning_provider_profile_for_model(model);
      provider && provider->api_family == model.api_family && !provider->reasoning_request_parameters.empty()) {
    return provider->reasoning_request_parameters;
  }

  if (model.api_family == openai_responses_reasoning_profile().api_family) {
    return openai_responses_reasoning_profile().request_parameters;
  }
  if (model.api_family == openai_compatible_reasoning_content_profile().api_family &&
      model.reasoning_format == openai_compatible_reasoning_content_profile().format) {
    return openai_compatible_reasoning_content_profile().request_parameters;
  }
  if (model.api_family == anthropic_thinking_reasoning_profile().api_family) {
    return anthropic_thinking_reasoning_profile().request_parameters;
  }

  return "provider-defined";
}

}  // namespace ava::config
