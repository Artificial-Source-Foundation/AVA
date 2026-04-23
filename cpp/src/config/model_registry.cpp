#include "ava/config/model_registry.hpp"

#include <algorithm>
#include <cctype>
#include <mutex>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace ava::config {
namespace {

const char* kEmbeddedRegistryJson = R"JSON(
{
  "models": [
    {
      "id": "claude-opus-4.6",
      "provider": "anthropic",
      "name": "Claude Opus 4.6",
      "aliases": ["opus"],
      "capabilities": {"tool_call": true, "vision": true, "reasoning": true, "streaming": true, "loop_prone": false},
      "limits": {"context_window": 200000, "max_output": 64000},
      "cost": {"input_per_million": 5.0, "output_per_million": 25.0}
    },
    {
      "id": "claude-sonnet-4.5",
      "provider": "anthropic",
      "name": "Claude Sonnet 4.5",
      "aliases": ["sonnet"],
      "capabilities": {"tool_call": true, "vision": true, "reasoning": true, "streaming": true, "loop_prone": false},
      "limits": {"context_window": 200000, "max_output": 64000},
      "cost": {"input_per_million": 3.0, "output_per_million": 15.0}
    },
    {
      "id": "gpt-5.3-codex",
      "provider": "openai",
      "name": "GPT-5.3 Codex",
      "aliases": ["codex"],
      "capabilities": {"tool_call": true, "vision": true, "reasoning": true, "streaming": true, "loop_prone": false},
      "limits": {"context_window": 200000, "max_output": 64000},
      "cost": {"input_per_million": 2.0, "output_per_million": 8.0}
    },
    {
      "id": "gpt-5-mini",
      "provider": "openai",
      "name": "GPT-5 mini",
      "aliases": ["mini"],
      "capabilities": {"tool_call": true, "vision": false, "reasoning": false, "streaming": true, "loop_prone": false},
      "limits": {"context_window": 128000, "max_output": 16384},
      "cost": {"input_per_million": 0.3, "output_per_million": 1.2}
    },
    {
      "id": "glm-4.7",
      "provider": "zai",
      "name": "GLM 4.7",
      "aliases": ["glm"],
      "capabilities": {"tool_call": true, "vision": false, "reasoning": false, "streaming": true, "loop_prone": true},
      "limits": {"context_window": 128000, "max_output": 8192},
      "cost": {"input_per_million": 0.2, "output_per_million": 0.8}
    },
    {
      "id": "mercury-2",
      "provider": "inception",
      "name": "Mercury 2",
      "aliases": ["mercury"],
      "capabilities": {"tool_call": true, "vision": false, "reasoning": false, "streaming": true, "loop_prone": true},
      "limits": {"context_window": 65536, "max_output": 4096},
      "cost": {"input_per_million": 0.1, "output_per_million": 0.4}
    }
  ]
}
)JSON";

[[nodiscard]] std::string lowercase(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return value;
}

[[nodiscard]] std::string normalize_token(const std::string& value) {
  std::string normalized;
  normalized.reserve(value.size());
  for(const auto ch : value) {
    if(ch != '.' && ch != '-' && ch != '_') {
      normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
  }
  return normalized;
}

}  // namespace

ModelRegistry ModelRegistry::load_embedded() {
  const auto parsed = nlohmann::json::parse(kEmbeddedRegistryJson);

  ModelRegistry registry;
  for(const auto& model : parsed.at("models")) {
    RegisteredModel entry;
    entry.id = model.at("id").get<std::string>();
    entry.provider = model.at("provider").get<std::string>();
    entry.name = model.at("name").get<std::string>();
    entry.aliases = model.value("aliases", std::vector<std::string>{});

    const auto& capabilities = model.at("capabilities");
    entry.capabilities.tool_call = capabilities.value("tool_call", false);
    entry.capabilities.vision = capabilities.value("vision", false);
    entry.capabilities.reasoning = capabilities.value("reasoning", false);
    entry.capabilities.streaming = capabilities.value("streaming", false);
    entry.capabilities.loop_prone = capabilities.value("loop_prone", false);

    const auto& limits = model.at("limits");
    entry.limits.context_window = limits.value("context_window", 0U);
    if(limits.contains("max_output") && !limits.at("max_output").is_null()) {
      entry.limits.max_output = limits.at("max_output").get<std::size_t>();
    }

    const auto& cost = model.at("cost");
    entry.cost.input_per_million = cost.value("input_per_million", 0.0);
    entry.cost.output_per_million = cost.value("output_per_million", 0.0);
    if(cost.contains("cache_read_per_million") && !cost.at("cache_read_per_million").is_null()) {
      entry.cost.cache_read_per_million = cost.at("cache_read_per_million").get<double>();
    }
    if(cost.contains("cache_write_per_million") && !cost.at("cache_write_per_million").is_null()) {
      entry.cost.cache_write_per_million = cost.at("cache_write_per_million").get<double>();
    }

    registry.models_.push_back(std::move(entry));
  }

  if(registry.models_.empty()) {
    throw std::runtime_error("Embedded model registry must not be empty");
  }

  return registry;
}

const RegisteredModel* ModelRegistry::find(const std::string& query) const {
  const auto lowered = lowercase(query);
  for(const auto& model : models_) {
    if(lowercase(model.id) == lowered) {
      return &model;
    }
    if(std::any_of(model.aliases.begin(), model.aliases.end(), [&](const auto& alias) {
         return lowercase(alias) == lowered;
       })) {
      return &model;
    }
  }
  return nullptr;
}

const RegisteredModel* ModelRegistry::find_for_provider(
    const std::string& provider,
    const std::string& model
) const {
  const auto lowered = lowercase(model);
  for(const auto& entry : models_) {
    if(entry.provider != provider) {
      continue;
    }
    if(lowercase(entry.id) == lowered) {
      return &entry;
    }
    if(std::any_of(entry.aliases.begin(), entry.aliases.end(), [&](const auto& alias) {
         return lowercase(alias) == lowered;
       })) {
      return &entry;
    }
  }
  return nullptr;
}

std::optional<std::pair<double, double>> ModelRegistry::pricing(const std::string& model) const {
  const auto lowered = lowercase(model);
  std::vector<const RegisteredModel*> matches;
  for(const auto& entry : models_) {
    const auto match_id = lowercase(entry.id) == lowered;
    const auto match_alias = std::any_of(entry.aliases.begin(), entry.aliases.end(), [&](const auto& alias) {
      return lowercase(alias) == lowered;
    });
    if(match_id || match_alias) {
      matches.push_back(&entry);
    }
  }

  if(matches.empty()) {
    return std::nullopt;
  }

  const auto it = std::find_if(matches.begin(), matches.end(), [](const RegisteredModel* value) {
    return value->cost.input_per_million > 0.0 || value->cost.output_per_million > 0.0;
  });
  const auto* winner = it != matches.end() ? *it : matches.front();
  return std::make_pair(winner->cost.input_per_million, winner->cost.output_per_million);
}

std::vector<const RegisteredModel*> ModelRegistry::models_for_provider(const std::string& provider) const {
  std::vector<const RegisteredModel*> models;
  for(const auto& entry : models_) {
    if(entry.provider == provider) {
      models.push_back(&entry);
    }
  }
  return models;
}

bool ModelRegistry::is_loop_prone(const std::string& model) const {
  if(const auto* known = find(model); known != nullptr) {
    return known->capabilities.loop_prone;
  }

  const auto lowered = lowercase(model);
  return lowered.find("glm") != std::string::npos || lowered.find("codegeex") != std::string::npos
      || lowered.find("minimax") != std::string::npos || lowered.find("kimi") != std::string::npos
      || lowered.find("mercury") != std::string::npos;
}

std::optional<std::string> ModelRegistry::normalize(const std::string& query) const {
  if(const auto* exact = find(query); exact != nullptr) {
    return exact->id;
  }

  const auto normalized = normalize_token(query);
  for(const auto& entry : models_) {
    if(normalize_token(entry.id) == normalized) {
      return entry.id;
    }
    if(std::any_of(entry.aliases.begin(), entry.aliases.end(), [&](const auto& alias) {
         return normalize_token(alias) == normalized;
       })) {
      return entry.id;
    }
  }
  return std::nullopt;
}

const ModelRegistry& registry() {
  static std::once_flag once;
  static ModelRegistry instance;
  std::call_once(once, [] { instance = ModelRegistry::load_embedded(); });
  return instance;
}

}  // namespace ava::config
