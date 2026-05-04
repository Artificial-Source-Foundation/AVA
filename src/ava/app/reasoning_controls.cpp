#include "ava/app/reasoning_controls.h"

#include <algorithm>
#include <optional>
#include <string>
#include <utility>

#include "ava/config/provider_profiles.h"
#include "ava/core/error.h"

namespace ava::app {
namespace {

std::string reasoning_selected_status(const RuntimeReasoningSelection& selection) {
  std::string status = "reasoning set to " + selection.level;
  if (selection.budget_tokens) status += " budget " + std::to_string(*selection.budget_tokens);
  if (!selection.display.empty()) status += " display " + selection.display;
  return status;
}

}  // namespace

std::optional<std::string> reasoning_status_for_session(const RuntimeSession& session) {
  const auto& model = session.model;
  if (!model.supports_reasoning.value_or(false) || model.reasoning_levels.empty()) return std::nullopt;
  if (!session.reasoning || session.reasoning->level.empty()) return std::nullopt;
  return session.reasoning->level;
}

ava::core::Result<RuntimeReasoningSelection> reasoning_selection_for_level(const ava::config::ModelInfo& model,
                                                                           std::string level) {
  auto profile = ava::config::reasoning_provider_profile_for_model(model);
  if (!profile || !profile->enabled_reasoning_requires_budget_tokens || level != "enabled") {
    return RuntimeReasoningSelection{.level = std::move(level), .budget_tokens = std::nullopt, .display = {}};
  }

  const auto default_budget = profile->default_reasoning_budget_tokens > 0 ? profile->default_reasoning_budget_tokens
                                                                           : profile->minimum_reasoning_budget_tokens;
  const auto max_output = model.max_output_tokens.value_or(default_budget);
  if (max_output <= profile->minimum_reasoning_budget_tokens) {
    const auto provider_label = ava::config::provider_display_name(model.provider_id);
    auto error =
        ava::core::Error(ava::core::ErrorCategory::InvalidArgument,
                         "reasoning unavailable: " + provider_label + " thinking needs output room for budget_tokens");
    error.with_context("provider", model.provider_id);
    error.with_context("model", model.model_id);
    error.with_context("max_output", std::to_string(max_output));
    return std::unexpected(std::move(error));
  }
  const auto budget = std::min<long long>(default_budget, max_output - 1);
  return RuntimeReasoningSelection{.level = std::move(level), .budget_tokens = budget, .display = {}};
}

ava::core::Result<std::string> cycle_runtime_reasoning(RuntimeSession& session) {
  const auto& model = session.model;
  if (!model.supports_reasoning.value_or(false)) {
    return std::string("reasoning unavailable: current model does not declare reasoning support");
  }
  if (model.reasoning_levels.empty()) {
    return std::string("reasoning unavailable: current model does not declare reasoning levels");
  }

  std::optional<std::size_t> current_index;
  if (session.reasoning) {
    for (std::size_t index = 0; index < model.reasoning_levels.size(); ++index) {
      if (model.reasoning_levels[index] == session.reasoning->level) {
        current_index = index;
        break;
      }
    }
  }

  if (current_index && *current_index + 1 >= model.reasoning_levels.size()) {
    auto cleared = set_runtime_reasoning(session, std::nullopt);
    if (!cleared) return std::unexpected(std::move(cleared.error()));
    return *cleared ? std::string("reasoning cleared") : std::string("reasoning already cleared");
  }

  const auto next_index = current_index ? *current_index + 1 : std::size_t{0};
  if (model.reasoning_levels[next_index] == "disabled") {
    auto cleared = set_runtime_reasoning(session, std::nullopt);
    if (!cleared) return std::unexpected(std::move(cleared.error()));
    return *cleared ? std::string("reasoning cleared") : std::string("reasoning already cleared");
  }

  auto selection = reasoning_selection_for_level(model, model.reasoning_levels[next_index]);
  if (!selection) return std::unexpected(std::move(selection.error()));
  auto selected = set_runtime_reasoning(session, *selection);
  if (!selected) return std::unexpected(std::move(selected.error()));
  return *selected ? reasoning_selected_status(*selection) : "reasoning already " + selection->level;
}

}  // namespace ava::app
