#include "ava/app/rpc/serialization_json.h"
#include "ava/app/rpc/serialization_models.h"
#include "ava/app/runtime.h"
#include "ava/config/model_config.h"
#include "ava/provider/registry.h"

#include <algorithm>

namespace ava::app::rpc {
namespace {

std::string model_key(std::string_view provider_id, std::string_view model_id)
{
  return std::string(provider_id) + "\n" + std::string(model_id);
}

bool has_model_key(std::vector<std::string> const& keys, std::string_view key)
{
  for (auto const& existing : keys)
  {
    if (existing == key)
      return true;
  }
  return false;
}

}  // namespace

std::vector<ava::config::ModelInfo> effective_models(ava::config::ModelRegistry const& registry)
{
  std::vector<ava::config::ModelInfo> models;
  std::vector<std::string> seen;
  for (auto model = registry.models.rbegin(); model != registry.models.rend(); ++model)
  {
    auto const key = model_key(model->provider_id, model->model_id);
    if (has_model_key(seen, key))
      continue;
    seen.push_back(key);
    models.push_back(*model);
  }
  std::reverse(models.begin(), models.end());
  return models;
}

std::string model_info_json(ava::config::ModelInfo const& model, ava::app::RuntimeSession const& session, bool configured)
{
  bool const registered = ava::provider::builtin_provider_registry().contains(model.provider_id);
  std::string json = "{";
  json += string_field_json("provider", model.provider_id);
  json += ',';
  json += string_field_json("model", model.model_id);
  json += ',';
  json += string_field_json("display_name", model.display_name);
  json += ',';
  json += string_field_json("family", model.family);
  json += ',';
  json += string_field_json("api_family", model.api_family);
  json += ',';
  json += bool_field_json("registered", registered);
  json += ',';
  json += bool_field_json("selectable", registered && configured);
  append_optional_integer(json, "context_window_tokens", model.context_window_tokens);
  append_optional_integer(json, "max_output_tokens", model.max_output_tokens);
  append_optional_bool(json, "supports_tools", model.supports_tools);
  append_optional_bool(json, "supports_streaming", model.supports_streaming);
  append_optional_bool(json, "supports_reasoning", model.supports_reasoning);
  append_optional_bool(json, "reports_usage", model.reports_usage);
  json += ",\"input_modalities\":";
  json += string_array_json(model.input_modalities);
  json += ",\"output_modalities\":";
  json += string_array_json(model.output_modalities);
  json += ",\"reasoning_levels\":";
  json += string_array_json(model.reasoning_levels);
  if (!model.reasoning_format.empty())
  {
    json += ',';
    json += string_field_json("reasoning_format", model.reasoning_format);
  }
  json += ",\"compatibility_quirks\":";
  json += string_array_json(model.compatibility_quirks);
  json += ',';
  json += bool_field_json("selected", session.model.provider_id == model.provider_id && session.model.model_id == model.model_id);
  json += '}';
  return json;
}

}  // namespace ava::app::rpc
