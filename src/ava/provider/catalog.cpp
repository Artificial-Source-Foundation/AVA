#include "sys.h"
#include "ava/provider/catalog.h"

#include <utility>

namespace ava::provider {
namespace {

ava::core::Error catalog_error(std::string message, std::string_view field = {})
{
  auto error = ava::core::Error(ava::core::ErrorCategory::Configuration, std::move(message));
  if (!field.empty())
    error.with_context("field", std::string(field));
  return error;
}

}  // namespace

ProviderCatalog::ProviderCatalog(ProviderRegistry registry, std::vector<ava::config::ProviderProfile> profiles,
                                 std::vector<ava::config::UserProviderDefinition> user_definitions)
    : registry_(std::move(registry)), profiles_(std::move(profiles)), user_definitions_(std::move(user_definitions))
{
}

ava::core::Result<std::shared_ptr<ProviderCatalog const>> ProviderCatalog::build(ava::config::XdgPaths const& paths)
{
  auto user_definitions = ava::config::load_user_provider_definitions(paths);
  if (!user_definitions)
    return std::unexpected(std::move(user_definitions.error()));
  if (auto collision = ava::config::validate_user_provider_ids_against_builtins(*user_definitions); !collision)
    return std::unexpected(std::move(collision.error()));

  // Phase B: retain validated user definitions for Phase C activation, but do not
  // register factories/adapters for them yet.
  auto registry = builtin_provider_registry();
  auto profiles = ava::config::builtin_provider_profiles();
  return std::shared_ptr<ProviderCatalog const>(new ProviderCatalog(std::move(registry), std::move(profiles), std::move(*user_definitions)));
}

std::shared_ptr<ProviderCatalog const> ProviderCatalog::build_builtins_only()
{
  return std::shared_ptr<ProviderCatalog const>(new ProviderCatalog(builtin_provider_registry(), ava::config::builtin_provider_profiles(), {}));
}

bool ProviderCatalog::contains(std::string_view provider_id) const noexcept
{
  return registry_.contains(provider_id);
}

ava::core::Result<std::unique_ptr<Provider>> ProviderCatalog::create(std::string_view provider_id) const
{
  return registry_.create(provider_id);
}

std::vector<std::string> ProviderCatalog::provider_ids() const
{
  return registry_.provider_ids();
}

std::span<ava::config::ProviderProfile const> ProviderCatalog::profiles() const noexcept
{
  return profiles_;
}

std::optional<ava::config::ProviderProfile> ProviderCatalog::find_profile(std::string_view provider_id) const
{
  for (auto const& profile : profiles_)
  {
    if (profile.provider_id == provider_id)
      return profile;
  }
  return std::nullopt;
}

std::optional<ava::config::ProviderProfile> ProviderCatalog::profile_for_model(ava::config::ModelInfo const& model) const
{
  return find_profile(model.provider_id);
}

std::optional<ava::config::ProviderProfile> ProviderCatalog::reasoning_profile_for_model(ava::config::ModelInfo const& model) const
{
  // Delegate to the shared profile/reasoning rules so built-in behavior stays
  // byte-compatible. Catalog construction is the only production entry that
  // materializes the builtin tables.
  return ava::config::reasoning_provider_profile_for_model(model);
}

std::string ProviderCatalog::display_name(std::string_view provider_id) const
{
  if (auto profile = find_profile(provider_id))
    return profile->display_name;
  return ava::config::provider_display_name(provider_id);
}

bool ProviderCatalog::accepts_reasoning_format(ava::config::ModelInfo const& model, std::string_view format) const
{
  return ava::config::provider_accepts_reasoning_format(model, format);
}

ava::core::VoidResult ProviderCatalog::validate_reasoning_request(ava::config::ModelInfo const& model, std::string_view level,
                                                                  std::optional<long long> budget_tokens, std::string_view display) const
{
  return ava::config::validate_reasoning_request(model, level, budget_tokens, display);
}

ava::core::VoidResult ProviderCatalog::validate_active_model(ava::config::ModelInfo const& model) const
{
  if (model.provider_id.empty() || model.model_id.empty())
    return std::unexpected(catalog_error("active model requires provider and model ids"));
  if (!contains(model.provider_id))
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::NotFound, "provider is not registered");
    error.with_context("provider", model.provider_id);
    error.with_context("model", model.model_id);
    return std::unexpected(std::move(error));
  }
  auto profile = find_profile(model.provider_id);
  if (profile && !profile->api_family.empty() && !model.api_family.empty() && profile->api_family != model.api_family)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::Configuration, "model api_family does not match provider catalog descriptor");
    error.with_context("provider", model.provider_id);
    error.with_context("model", model.model_id);
    error.with_context("model_api_family", model.api_family);
    error.with_context("provider_api_family", profile->api_family);
    return std::unexpected(std::move(error));
  }
  return {};
}

bool ProviderCatalog::provider_has_runtime_factory(std::string_view provider_id) const noexcept
{
  return contains(provider_id);
}

std::span<ava::config::UserProviderDefinition const> ProviderCatalog::user_definitions() const noexcept
{
  return user_definitions_;
}

ava::core::Result<std::shared_ptr<ProviderCatalog const>> ensure_provider_catalog(std::shared_ptr<ProviderCatalog const> existing,
                                                                                  ava::config::XdgPaths const& paths)
{
  if (existing)
    return existing;
  return ProviderCatalog::build(paths);
}

}  // namespace ava::provider
