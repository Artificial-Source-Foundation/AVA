#include "sys.h"
#include "ava/app/runtime_json.h"
#include "ava/app/runtime_reasoning.h"
#include "ava/config/provider_profiles.h"
#include "ava/core/json.h"

#include <algorithm>
#include <utility>

namespace ava::app::runtime {
namespace {

bool same_reasoning_selection(std::optional<ReasoningSelection> const& left, std::optional<ReasoningSelection> const& right)
{
  if (!left || !right)
    return !left && !right;
  return left->level == right->level && left->provider_level == right->provider_level && left->budget_tokens == right->budget_tokens &&
         left->display == right->display;
}

ava::core::Result<ReasoningSelection> resolve_runtime_reasoning_selection(ava::config::ModelInfo const& model, ReasoningSelection selection)
{
  auto const level = trim(selection.level);
  if (level.empty())
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "reasoning level is required"));
  }
  auto const resolved = ava::config::resolve_reasoning_level(model, level);
  if (!resolved.supported)
  {
    auto error =
        ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "reasoning level is not supported by the current model's supported reasoning levels");
    error.with_context("provider", model.provider_id);
    error.with_context("model", model.model_id);
    error.with_context("level", std::string(level));
    return std::unexpected(std::move(error));
  }
  if (!model.supports_reasoning.value_or(false) && !resolved.explicit_mapping)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "current model does not declare reasoning support");
    error.with_context("provider", model.provider_id);
    error.with_context("model", model.model_id);
    return std::unexpected(std::move(error));
  }
  if (selection.budget_tokens && *selection.budget_tokens <= 0)
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "reasoning budget must be positive"));
  }
  auto const provider_level = resolved.provider_level.value_or(std::string(level));
  if (auto valid = ava::config::validate_reasoning_request(model, provider_level, selection.budget_tokens, selection.display); !valid)
  {
    return std::unexpected(std::move(valid.error()));
  }
  selection.level = std::string(level);
  selection.provider_level = resolved.provider_level;
  return selection;
}

}  // namespace

ava::provider::ProviderReasoningOptions provider_reasoning_options(ReasoningSelection const& selection)
{
  return ava::provider::ProviderReasoningOptions{
      .type = selection.provider_level.value_or(selection.level), .budget_tokens = selection.budget_tokens, .display = selection.display};
}

std::optional<ReasoningSelection> latest_persisted_reasoning(std::vector<ava::session::SessionEntry> const& entries, ava::config::ModelInfo const& model)
{
  std::optional<ReasoningSelection> latest;
  bool saw_change = false;
  for (auto const& entry : entries)
  {
    if (entry.type == ava::session::EntryType::SessionStart || entry.type == ava::session::EntryType::ModelChange)
    {
      latest = std::nullopt;
      continue;
    }
    if (entry.type != ava::session::EntryType::ReasoningChange)
      continue;
    auto provider = ava::core::json::string_field(entry.data_json, "provider");
    auto model_id = ava::core::json::string_field(entry.data_json, "model");
    if (!provider || !model_id || *provider != model.provider_id || *model_id != model.model_id)
    {
      saw_change = true;
      latest = std::nullopt;
      continue;
    }
    saw_change = true;
    if (bool_json_field(entry.data_json, "enabled") == false)
    {
      latest = std::nullopt;
      continue;
    }
    auto level = ava::core::json::string_field(entry.data_json, "level").value_or("");
    if (level.empty())
    {
      latest = std::nullopt;
      continue;
    }
    latest = ReasoningSelection{.level = std::move(level),
                                       .provider_level = std::nullopt,
                                       .budget_tokens = ava::core::json::integer_field(entry.data_json, "budget_tokens"),
                                       .display = ava::core::json::string_field(entry.data_json, "display").value_or("")};
  }
  if (!saw_change || !latest)
    return std::nullopt;
  auto resolved = resolve_runtime_reasoning_selection(model, *latest);
  if (!resolved)
    return std::nullopt;
  return *resolved;
}

}  // namespace ava::app::runtime

namespace ava::app {

ava::core::Result<bool> set_runtime_reasoning(runtime::Session& session, std::optional<runtime::ReasoningSelection> selection)
{
  if (selection)
  {
    selection->level = runtime::trimmed_copy(selection->level);
    selection->display = runtime::trimmed_copy(selection->display);
    auto resolved = runtime::resolve_runtime_reasoning_selection(session.model, std::move(*selection));
    if (!resolved)
    {
      return std::unexpected(std::move(resolved.error()));
    }
    selection = std::move(*resolved);
  }
  if (runtime::same_reasoning_selection(session.reasoning, selection))
    return false;

  auto appended = session.append_owned(runtime::make_reasoning_change_entry(session.model, selection));
  if (!appended)
    return std::unexpected(std::move(appended.error()));
  session.reasoning = std::move(selection);
  return true;
}

}  // namespace ava::app
