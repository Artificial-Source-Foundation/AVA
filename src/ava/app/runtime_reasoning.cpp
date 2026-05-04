#include "ava/app/runtime_reasoning.h"

#include <algorithm>
#include <utility>

#include "ava/app/runtime_json.h"
#include "ava/config/provider_profiles.h"
#include "ava/core/json.h"

namespace ava::app::runtime {
namespace {

bool has_reasoning_level(ava::config::ModelInfo const& model, std::string_view level)
{
  return std::ranges::find(model.reasoning_levels, level) != model.reasoning_levels.end();
}

bool same_reasoning_selection(std::optional<RuntimeReasoningSelection> const& left,
                              std::optional<RuntimeReasoningSelection> const& right)
{
  if (!left || !right) return !left && !right;
  return left->level == right->level && left->budget_tokens == right->budget_tokens && left->display == right->display;
}

ava::core::VoidResult validate_reasoning_selection(ava::config::ModelInfo const& model,
                                                   RuntimeReasoningSelection const& selection)
{
  auto const level = trim(selection.level);
  if (level.empty()) {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "reasoning level is required"));
  }
  if (!model.supports_reasoning.value_or(false)) {
    auto error =
        ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "current model does not declare reasoning support");
    error.with_context("provider", model.provider_id);
    error.with_context("model", model.model_id);
    return std::unexpected(std::move(error));
  }
  if (model.reasoning_levels.empty()) {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument,
                                  "current model does not declare supported reasoning levels");
    error.with_context("provider", model.provider_id);
    error.with_context("model", model.model_id);
    return std::unexpected(std::move(error));
  }
  if (!model.reasoning_levels.empty() && !has_reasoning_level(model, level)) {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument,
                                  "reasoning level is not supported by the current model");
    error.with_context("provider", model.provider_id);
    error.with_context("model", model.model_id);
    error.with_context("level", std::string(level));
    return std::unexpected(std::move(error));
  }
  if (selection.budget_tokens && *selection.budget_tokens <= 0) {
    return std::unexpected(
        ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "reasoning budget must be positive"));
  }
  if (auto valid = ava::config::validate_reasoning_request(model, level, selection.budget_tokens, selection.display);
      !valid) {
    return std::unexpected(std::move(valid.error()));
  }
  return {};
}

}  // namespace

ava::provider::ProviderReasoningOptions provider_reasoning_options(RuntimeReasoningSelection const& selection)
{
  return ava::provider::ProviderReasoningOptions{
      .type = selection.level, .budget_tokens = selection.budget_tokens, .display = selection.display};
}

std::optional<RuntimeReasoningSelection> latest_persisted_reasoning(
    std::vector<ava::session::SessionEntry> const& entries, ava::config::ModelInfo const& model)
{
  std::optional<RuntimeReasoningSelection> latest;
  bool saw_change = false;
  for (auto const& entry : entries) {
    if (entry.type == ava::session::EntryType::SessionStart || entry.type == ava::session::EntryType::ModelChange) {
      latest = std::nullopt;
      continue;
    }
    if (entry.type != ava::session::EntryType::ReasoningChange) continue;
    auto provider = ava::core::json::string_field(entry.data_json, "provider");
    auto model_id = ava::core::json::string_field(entry.data_json, "model");
    if (!provider || !model_id || *provider != model.provider_id || *model_id != model.model_id) {
      saw_change = true;
      latest = std::nullopt;
      continue;
    }
    saw_change = true;
    if (bool_json_field(entry.data_json, "enabled") == false) {
      latest = std::nullopt;
      continue;
    }
    auto level = ava::core::json::string_field(entry.data_json, "level").value_or("");
    if (level.empty()) {
      latest = std::nullopt;
      continue;
    }
    latest =
        RuntimeReasoningSelection{.level = std::move(level),
                                  .budget_tokens = ava::core::json::integer_field(entry.data_json, "budget_tokens"),
                                  .display = ava::core::json::string_field(entry.data_json, "display").value_or("")};
  }
  if (!saw_change || !latest) return std::nullopt;
  if (auto valid = validate_reasoning_selection(model, *latest); !valid) return std::nullopt;
  return latest;
}

}  // namespace ava::app::runtime

namespace ava::app {

ava::core::Result<bool> set_runtime_reasoning(RuntimeSession& session,
                                              std::optional<RuntimeReasoningSelection> selection)
{
  if (selection) {
    selection->level = runtime::trimmed_copy(selection->level);
    selection->display = runtime::trimmed_copy(selection->display);
    if (auto valid = runtime::validate_reasoning_selection(session.model, *selection); !valid) {
      return std::unexpected(std::move(valid.error()));
    }
  }
  if (runtime::same_reasoning_selection(session.reasoning, selection)) return false;

  auto appended = runtime::append_reasoning_change(session.store, session.model, selection);
  if (!appended) return std::unexpected(std::move(appended.error()));
  session.reasoning = std::move(selection);
  return true;
}

}  // namespace ava::app
