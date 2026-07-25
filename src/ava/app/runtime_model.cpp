#include "sys.h"
#include "ava/app/runtime/Session.h"
#include "ava/app/runtime_json.h"
#include "ava/app/runtime_model.h"
#include "ava/app/runtime_prompt.h"
#include "ava/config/provider_profiles.h"
#include "ava/session/assistant_output.h"
#include "ava/session/validation.h"
#include "ava/provider/registry.h"
#include "ava/core/json.h"
#include "ava/core/string_utils.h"

#include <utility>

namespace ava::app::runtime {
namespace {

ava::config::ModelInfo fallback_persisted_model(std::string provider_id, std::string model_id, std::string display_name, std::string family,
                                                std::string api_family, std::optional<long long> context_window_tokens,
                                                std::optional<long long> max_output_tokens, std::optional<bool> supports_tools,
                                                std::optional<bool> supports_streaming, std::optional<bool> supports_reasoning,
                                                std::optional<bool> reports_usage, std::vector<std::string> input_modalities,
                                                std::vector<std::string> output_modalities, std::vector<std::string> reasoning_levels,
                                                std::vector<std::string> compatibility_quirks, std::string reasoning_format)
{
  if (display_name.empty())
    display_name = model_id;
  if (family.empty())
    family = model_id;
  if (compatibility_quirks.empty())
    compatibility_quirks = {"persisted_unknown_model"};
  return ava::config::ModelInfo{.provider_id = std::move(provider_id),
                                .model_id = std::move(model_id),
                                .display_name = std::move(display_name),
                                .family = std::move(family),
                                .context_window_tokens = context_window_tokens,
                                .max_output_tokens = max_output_tokens,
                                .pricing = std::nullopt,
                                .api_family = std::move(api_family),
                                .input_modalities = std::move(input_modalities),
                                .supports_tools = supports_tools,
                                .supports_streaming = supports_streaming,
                                .supports_reasoning = supports_reasoning,
                                .reports_usage = reports_usage,
                                .reasoning_levels = std::move(reasoning_levels),
                                .compatibility_quirks = std::move(compatibility_quirks),
                                .output_modalities = std::move(output_modalities),
                                .reasoning_format = std::move(reasoning_format)};
}

}  // namespace

std::optional<ava::config::ModelInfo> latest_persisted_model(ava::config::ModelRegistry const& registry, std::vector<ava::session::SessionEntry> const& entries)
{
  std::optional<std::string> provider_id;
  std::optional<std::string> model_id;
  std::string display_name;
  std::string family;
  std::string api_family;
  std::optional<long long> context_window_tokens;
  std::optional<long long> max_output_tokens;
  std::optional<bool> supports_tools;
  std::optional<bool> supports_streaming;
  std::optional<bool> supports_reasoning;
  std::optional<bool> reports_usage;
  std::vector<std::string> input_modalities;
  std::vector<std::string> output_modalities;
  std::vector<std::string> reasoning_levels;
  std::vector<std::string> compatibility_quirks;
  std::string reasoning_format;
  for (auto const& entry : entries)
  {
    if (entry.type != ava::session::EntryType::SessionStart && entry.type != ava::session::EntryType::ModelChange)
    {
      continue;
    }
    auto provider = ava::core::json::string_field(entry.data_json, "provider");
    auto model = ava::core::json::string_field(entry.data_json, "model");
    if (!provider || !model || provider->empty() || model->empty())
      continue;
    provider_id = std::move(*provider);
    model_id = std::move(*model);
    display_name = ava::core::json::string_field(entry.data_json, "display_name").value_or("");
    family = ava::core::json::string_field(entry.data_json, "family").value_or("");
    api_family = ava::core::json::string_field(entry.data_json, "api_family").value_or("");
    context_window_tokens = ava::core::json::integer_field(entry.data_json, "context_window_tokens");
    max_output_tokens = ava::core::json::integer_field(entry.data_json, "max_output_tokens");
    supports_tools = bool_json_field(entry.data_json, "supports_tools");
    supports_streaming = bool_json_field(entry.data_json, "supports_streaming");
    supports_reasoning = bool_json_field(entry.data_json, "supports_reasoning");
    reports_usage = bool_json_field(entry.data_json, "reports_usage");
    input_modalities = string_array_field(entry.data_json, "input_modalities");
    output_modalities = string_array_field(entry.data_json, "output_modalities");
    reasoning_levels = string_array_field(entry.data_json, "reasoning_levels");
    compatibility_quirks = string_array_field(entry.data_json, "compatibility_quirks");
    reasoning_format = ava::core::json::string_field(entry.data_json, "reasoning_format").value_or("");
  }
  if (!provider_id || !model_id)
    return std::nullopt;
  if (auto model = ava::config::find_model(registry, *provider_id, *model_id))
    return model;
  return fallback_persisted_model(std::move(*provider_id), std::move(*model_id), std::move(display_name), std::move(family), std::move(api_family),
                                  context_window_tokens, max_output_tokens, supports_tools, supports_streaming, supports_reasoning, reports_usage,
                                  std::move(input_modalities), std::move(output_modalities), std::move(reasoning_levels), std::move(compatibility_quirks),
                                  std::move(reasoning_format));
}

}  // namespace ava::app::runtime

namespace ava::app {

ava::core::Result<ava::config::ModelInfo> resolve_runtime_model(ava::config::XdgPaths const& paths, std::string_view provider_id, std::string_view model_id)
{
  auto const trimmed_provider_id = core::trim(provider_id);
  auto const trimmed_model_id = core::trim(model_id);
  if (trimmed_provider_id.empty() || trimmed_model_id.empty())
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "provider and model are required"));
  }

  auto const providers = ava::provider::builtin_provider_registry();
  if (!providers.contains(trimmed_provider_id))
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::NotFound, "provider is not registered");
    error.with_context("provider", trimmed_provider_id);
    return std::unexpected(std::move(error));
  }

  auto registry = ava::config::load_model_registry(paths);
  if (!registry)
    return std::unexpected(std::move(registry.error()));
  auto model = ava::config::find_model(*registry, trimmed_provider_id, trimmed_model_id);
  if (!model)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::NotFound, "model is not configured");
    error.with_context("provider", trimmed_provider_id);
    error.with_context("model", trimmed_model_id);
    return std::unexpected(std::move(error));
  }
  return *model;
}

ava::core::Result<bool> switch_runtime_model(runtime::Session& session, ava::config::ModelInfo model)
{
  if (session.model.provider_id == model.provider_id && session.model.model_id == model.model_id)
    return false;

  auto prompt_state = runtime::load_runtime_prompt_state(session.paths(), model, session.mode(), session.workspace_dir(), session.current_dir(),
                                                         project_resources_trusted(session.project_trust), session.prompt_overrides());
  if (!prompt_state)
    return std::unexpected(std::move(prompt_state.error()));

  auto const previous = session.model;
  auto appended = session.append_owned(runtime::make_model_change_entry(previous, model));
  if (!appended)
    return std::unexpected(std::move(appended.error()));

  session.model = std::move(model);
  session.invocation_inputs().mode = prompt_state->mode;
  session.base_prompt = std::move(prompt_state->base_prompt);
  session.context_sources = std::move(prompt_state->context_sources);
  session.freshness_sources = std::move(prompt_state->freshness_sources);
  session.system_prompt = std::move(prompt_state->system_prompt);
  session.reasoning = std::nullopt;
  if (auto refreshed = refresh_runtime_parent_configuration(session); !refreshed)
    return std::unexpected(std::move(refreshed.error()));
  return true;
}

}  // namespace ava::app
