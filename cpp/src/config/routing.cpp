#include "ava/config/routing.hpp"

#include <stdexcept>

#include "ava/core/string_utils.hpp"

namespace ava::config {
namespace {

[[nodiscard]] std::optional<std::string> optional_trimmed_string(const nlohmann::json& j, const char* key) {
  if(!j.contains(key) || j.at(key).is_null() || !j.at(key).is_string()) {
    return std::nullopt;
  }
  return std::string(ava::core::trim_ascii_view(j.at(key).get<std::string>()));
}

[[nodiscard]] std::string routing_mode_to_string(RoutingMode value) {
  switch(value) {
    case RoutingMode::Off:
      return "off";
    case RoutingMode::Conservative:
      return "conservative";
  }
  return "off";
}

[[nodiscard]] std::string routing_profile_to_string(RoutingProfile value) {
  switch(value) {
    case RoutingProfile::Cheap:
      return "cheap";
    case RoutingProfile::Capable:
      return "capable";
  }
  return "cheap";
}

}  // namespace

void RoutingTarget::normalize() {
  if(provider.has_value()) {
    provider = ava::core::lowercase_ascii(std::string(ava::core::trim_ascii_view(*provider)));
  }
  if(model.has_value()) {
    model = std::string(ava::core::trim_ascii_view(*model));
  }
}

bool RoutingTarget::is_complete() const {
  return provider.has_value() && model.has_value() && !provider->empty() && !model->empty();
}

bool RoutingConfig::is_enabled() const {
  return mode != RoutingMode::Off;
}

void RoutingConfig::normalize() {
  targets.cheap.normalize();
  targets.capable.normalize();
}

const RoutingTarget* RoutingConfig::target_for(RoutingProfile profile) const {
  const auto* target = profile == RoutingProfile::Cheap ? &targets.cheap : &targets.capable;
  return target->is_complete() ? target : nullptr;
}

void to_json(nlohmann::json& j, RoutingMode value) {
  j = routing_mode_to_string(value);
}

void from_json(const nlohmann::json& j, RoutingMode& value) {
  const auto raw = ava::core::lowercase_ascii(j.get<std::string>());
  if(raw == "off") {
    value = RoutingMode::Off;
  } else if(raw == "conservative") {
    value = RoutingMode::Conservative;
  } else {
    throw std::invalid_argument("unknown routing mode: " + raw);
  }
}

void to_json(nlohmann::json& j, RoutingProfile value) {
  j = routing_profile_to_string(value);
}

void from_json(const nlohmann::json& j, RoutingProfile& value) {
  const auto raw = ava::core::lowercase_ascii(j.get<std::string>());
  if(raw == "cheap") {
    value = RoutingProfile::Cheap;
  } else if(raw == "capable") {
    value = RoutingProfile::Capable;
  } else {
    throw std::invalid_argument("unknown routing profile: " + raw);
  }
}

void to_json(nlohmann::json& j, const RoutingTarget& value) {
  j = nlohmann::json::object();
  if(value.provider.has_value()) {
    j["provider"] = *value.provider;
  }
  if(value.model.has_value()) {
    j["model"] = *value.model;
  }
}

void from_json(const nlohmann::json& j, RoutingTarget& value) {
  value.provider = optional_trimmed_string(j, "provider");
  value.model = optional_trimmed_string(j, "model");
}

void to_json(nlohmann::json& j, const RoutingTargets& value) {
  j = nlohmann::json{{"cheap", value.cheap}, {"capable", value.capable}};
}

void from_json(const nlohmann::json& j, RoutingTargets& value) {
  if(j.contains("cheap")) {
    value.cheap = j.at("cheap").get<RoutingTarget>();
  }
  if(j.contains("capable")) {
    value.capable = j.at("capable").get<RoutingTarget>();
  }
}

void to_json(nlohmann::json& j, const RoutingConfig& value) {
  j = nlohmann::json{{"mode", value.mode}, {"targets", value.targets}};
}

void from_json(const nlohmann::json& j, RoutingConfig& value) {
  if(j.contains("mode")) {
    value.mode = j.at("mode").get<RoutingMode>();
  }
  if(j.contains("targets")) {
    value.targets = j.at("targets").get<RoutingTargets>();
  }
}

}  // namespace ava::config
