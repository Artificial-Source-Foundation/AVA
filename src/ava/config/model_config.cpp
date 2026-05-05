#include "ava/config/model_config.h"

#include <optional>
#include <string>
#include <string_view>
#include <utility>

#include "ava/config/model_config_support.h"
#include "ava/config/model_profiles.h"
#include "ava/core/json.h"

namespace ava::config {

ModelRegistry builtin_model_registry()
{
  return builtin_model_profiles();
}

ModelRegistry parse_model_registry(std::string_view content)
{
  auto registry = builtin_model_registry();
  if (auto provider = ava::core::json::string_field(content, "default_provider"))
    registry.default_provider_id = *provider;
  if (auto model = ava::core::json::string_field(content, "default_model")) registry.default_model_id = *model;

  for (auto const& item : ava::core::json::objects_in_array_field(content, "models")) {
    auto provider = ava::core::json::string_field(item, "provider");
    auto id = ava::core::json::string_field(item, "id");
    if (!provider || !id) continue;
    auto model = find_model(registry, *provider, *id)
                     .value_or(ModelInfo{.provider_id = *provider,
                                         .model_id = *id,
                                         .display_name = *id,
                                         .family = detail::family_from_model_id(*id),
                                         .context_window_tokens = std::nullopt,
                                         .max_output_tokens = std::nullopt,
                                         .pricing = std::nullopt,
                                         .api_family = "",
                                         .input_modalities = {},
                                         .supports_tools = std::nullopt,
                                         .supports_streaming = std::nullopt,
                                         .supports_reasoning = std::nullopt,
                                         .reports_usage = std::nullopt,
                                         .reasoning_levels = {},
                                         .compatibility_quirks = {},
                                         .output_modalities = {},
                                         .reasoning_format = {}});
    model.provider_id = *provider;
    model.model_id = *id;
    if (auto name = ava::core::json::string_field(item, "name")) model.display_name = *name;
    if (auto family = ava::core::json::string_field(item, "family")) model.family = *family;
    if (auto context_window = detail::positive_integer_field(item, {"context_window_tokens", "context_window"})) {
      model.context_window_tokens = context_window;
    }
    if (auto max_output = detail::positive_integer_field(item, {"max_output_tokens"})) {
      model.max_output_tokens = max_output;
    }
    if (auto pricing = detail::model_pricing_from_item(item)) model.pricing = pricing;
    if (auto api_family = ava::core::json::string_field(item, "api_family")) model.api_family = *api_family;
    if (detail::has_any_field(item, {"input_modalities", "input"})) {
      model.input_modalities = detail::string_array_field(item, {"input_modalities", "input"});
    }
    if (detail::has_any_field(item, {"output_modalities", "output"})) {
      model.output_modalities = detail::string_array_field(item, {"output_modalities", "output"});
    }
    if (auto supports_tools = detail::bool_field(item, {"supports_tools", "tool_support", "tools"})) {
      model.supports_tools = supports_tools;
    }
    if (auto supports_streaming = detail::bool_field(item, {"supports_streaming", "streaming"})) {
      model.supports_streaming = supports_streaming;
    }
    if (auto supports_reasoning = detail::bool_field(item, {"supports_reasoning", "reasoning"})) {
      model.supports_reasoning = supports_reasoning;
    }
    if (auto reports_usage = detail::bool_field(item, {"reports_usage", "usage_support", "usage"})) {
      model.reports_usage = reports_usage;
    }
    if (detail::has_any_field(item, {"reasoning_levels"})) {
      model.reasoning_levels = detail::string_array_field(item, {"reasoning_levels"});
    }
    if (detail::has_any_field(item, {"compatibility_quirks", "quirks"})) {
      model.compatibility_quirks = detail::string_array_field(item, {"compatibility_quirks", "quirks"});
    }
    if (auto reasoning_format = ava::core::json::string_field(item, "reasoning_format")) {
      model.reasoning_format = *reasoning_format;
    }
    registry.models.push_back(std::move(model));
  }
  return registry;
}

ava::core::Result<ModelRegistry> load_model_registry(XdgPaths const& paths)
{
  if (!std::filesystem::exists(paths.models_file)) return builtin_model_registry();
  auto content = detail::read_model_config_text(paths.models_file);
  if (!content) return std::unexpected(content.error());
  return parse_model_registry(*content);
}

std::optional<ModelInfo> find_model(ModelRegistry const& registry, std::string_view provider_id,
                                    std::string_view model_id)
{
  for (auto it = registry.models.rbegin(); it != registry.models.rend(); ++it) {
    auto const& model = *it;
    if (model.provider_id == provider_id && model.model_id == model_id) return model;
  }
  return std::nullopt;
}

ModelInfo select_default_model(ModelRegistry const& registry)
{
  if (auto model = find_model(registry, registry.default_provider_id, registry.default_model_id)) return *model;
  return ModelInfo{
      .provider_id = registry.default_provider_id,
      .model_id = registry.default_model_id,
      .display_name = registry.default_model_id,
      .family = detail::family_from_model_id(registry.default_model_id),
      .context_window_tokens = std::nullopt,
      .max_output_tokens = std::nullopt,
      .pricing = std::nullopt,
      .api_family = "",
      .input_modalities = {},
      .supports_tools = true,
      .supports_streaming = true,
      .supports_reasoning = std::nullopt,
      .reports_usage = std::nullopt,
      .reasoning_levels = {},
      .compatibility_quirks = {},
      .output_modalities = {},
      .reasoning_format = {},
  };
}

std::optional<long double> usage_cost_usd(ModelPricing const& pricing, ava::provider::TokenUsage const& usage)
{
  return detail::billable_usage_cost_usd(pricing, usage);
}

}  // namespace ava::config
