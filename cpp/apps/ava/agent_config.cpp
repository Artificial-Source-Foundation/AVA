#include "agent_config.hpp"

#include <cstdint>
#include <stdexcept>

#include "ava/config/model_registry.hpp"
#include "ava/config/model_spec.hpp"
#include "ava/config/paths.hpp"
#include "ava/llm/factory.hpp"

namespace ava::app {
namespace {

[[nodiscard]] std::optional<std::string> metadata_string(
    const ava::types::SessionRecord& session,
    const char* key
) {
  if(!session.metadata.is_object()) {
    return std::nullopt;
  }
  if(!session.metadata.contains("headless") || !session.metadata.at("headless").is_object()) {
    return std::nullopt;
  }
  const auto& headless = session.metadata.at("headless");
  if(!headless.contains(key) || !headless.at(key).is_string()) {
    return std::nullopt;
  }
  return headless.at(key).get<std::string>();
}

[[nodiscard]] std::optional<std::size_t> metadata_max_turns(const ava::types::SessionRecord& session) {
  if(!session.metadata.is_object()) {
    return std::nullopt;
  }
  if(!session.metadata.contains("headless") || !session.metadata.at("headless").is_object()) {
    return std::nullopt;
  }
  const auto& headless = session.metadata.at("headless");
  if(!headless.contains("max_turns") || !headless.at("max_turns").is_number_integer()) {
    return std::nullopt;
  }

  const auto value = headless.at("max_turns").get<std::int64_t>();
  if(value <= 0) {
    return std::nullopt;
  }
  return static_cast<std::size_t>(value);
}

[[nodiscard]] std::string default_model_for_provider(const std::string& provider) {
  const auto models = ava::config::registry().models_for_provider(provider);
  if(!models.empty()) {
    return models.front()->id;
  }
  if(provider == "openai") {
    return "gpt-5-mini";
  }
  throw std::runtime_error("no default model known for provider: " + provider);
}

}  // namespace

ResolvedAgentSelection resolve_agent_selection(
    const CliOptions& cli,
    const ava::types::SessionRecord& session
) {
  const auto persisted_provider = metadata_string(session, "provider");
  const auto persisted_model = metadata_string(session, "model");

  std::optional<std::string> provider;
  std::optional<std::string> model;

  if(cli.model.has_value()) {
    if(cli.provider.has_value()) {
      provider = ava::config::normalize_provider_name(*cli.provider);
      model = *cli.model;
    } else {
      const auto parsed = ava::config::parse_model_spec(*cli.model);
      provider = ava::config::normalize_provider_name(parsed.provider);
      model = parsed.model;
    }
  }

  if(!provider.has_value() && cli.provider.has_value()) {
    provider = ava::config::normalize_provider_name(*cli.provider);
  }
  if(!model.has_value() && cli.model.has_value()) {
    model = *cli.model;
  }

  if(!provider.has_value() && persisted_provider.has_value()) {
    provider = ava::config::normalize_provider_name(*persisted_provider);
  }
  if(!model.has_value() && persisted_model.has_value()) {
    model = *persisted_model;
  }

  if(!provider.has_value()) {
    provider = std::string{"openai"};
  }
  if(!model.has_value()) {
    model = default_model_for_provider(*provider);
  }

  std::size_t max_turns = cli.max_turns;
  if(!cli.max_turns_explicit) {
    if(const auto persisted_turns = metadata_max_turns(session); persisted_turns.has_value()) {
      max_turns = *persisted_turns;
    }
  }

  return ResolvedAgentSelection{
      .provider = *provider,
      .model = *model,
      .max_turns = max_turns,
  };
}

ava::config::CredentialStore load_credentials_for_run() {
  return ava::config::CredentialStore::load(ava::config::credentials_path());
}

ava::llm::ProviderPtr build_provider_for_run(
    const ResolvedAgentSelection& selection,
    const ava::config::CredentialStore& credentials
) {
  return ava::llm::create_provider(selection.provider, selection.model, credentials);
}

}  // namespace ava::app
