#include "sys.h"
#include "ava/app/runtime/Session.h"
#include "ava/app/runtime_catalog.h"
#include "ava/provider/registry.h"

#include <algorithm>
#include <string>
#include <utility>

namespace ava::app {
namespace {

std::string model_key(std::string_view provider_id, std::string_view model_id)
{
  return std::string(provider_id) + "/" + std::string(model_id);
}

}  // namespace

ava::core::Result<std::unique_ptr<ava::provider::Provider>> create_runtime_provider(std::string_view provider_id)
{
  return ava::provider::builtin_provider_registry().create(provider_id);
}

ava::core::Result<std::vector<ava::config::ModelInfo>> runtime_model_catalog(runtime::Session const& session)
{
  auto registry = ava::config::load_model_registry(session.continuity.paths);
  if (!registry)
    return std::unexpected(std::move(registry.error()));
  auto providers = ava::provider::builtin_provider_registry();
  std::vector<ava::config::ModelInfo> registered;
  for (auto const& model : registry->models)
    if (providers.contains(model.provider_id))
      registered.push_back(model);

  if (!session.scoped_model_cycle)
    return registered;
  std::vector<ava::config::ModelInfo> scoped;
  scoped.reserve(session.scoped_model_cycle->size());
  for (auto const& id : *session.scoped_model_cycle)
  {
    auto found = std::ranges::find_if(registered, [&](auto const& model) { return id == model_key(model.provider_id, model.model_id); });
    if (found != registered.end())
      scoped.push_back(*found);
  }
  return scoped;
}

ava::core::Result<ava::config::ModelInfo> select_runtime_model(runtime::Session const& session, std::optional<std::string_view> provider_id,
                                                               std::string_view model_id)
{
  if (model_id.empty())
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "model is required"));
  if (provider_id && !provider_id->empty())
    return resolve_runtime_model(session.continuity.paths, *provider_id, model_id);
  if (auto current = resolve_runtime_model(session.continuity.paths, session.model.provider_id, model_id); current)
    return current;

  auto models = runtime_model_catalog(session);
  if (!models)
    return std::unexpected(std::move(models.error()));
  std::vector<ava::config::ModelInfo> matches;
  for (auto const& model : *models)
    if (model.model_id == model_id)
      matches.push_back(model);
  if (matches.empty())
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::NotFound, "model is not configured");
    error.with_context("model", std::string(model_id));
    return std::unexpected(std::move(error));
  }
  if (matches.size() != 1)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, "model id is ambiguous; provider is required");
    error.with_context("model", std::string(model_id));
    error.with_context("matches", std::to_string(matches.size()));
    return std::unexpected(std::move(error));
  }
  return matches.front();
}

ava::core::Result<ava::config::ModelInfo> cycle_runtime_model(runtime::Session const& session, int direction)
{
  auto models = runtime_model_catalog(session);
  if (!models)
    return std::unexpected(std::move(models.error()));
  if (models->empty())
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::NotFound, "no registered provider models are enabled for cycling"));
  std::size_t selected = direction < 0 ? models->size() - 1 : 0;
  for (std::size_t index = 0; index < models->size(); ++index)
  {
    if ((*models)[index].provider_id == session.model.provider_id && (*models)[index].model_id == session.model.model_id)
    {
      selected = direction < 0 ? (index == 0 ? models->size() - 1 : index - 1) : (index + 1) % models->size();
      break;
    }
  }
  return (*models)[selected];
}

}  // namespace ava::app
