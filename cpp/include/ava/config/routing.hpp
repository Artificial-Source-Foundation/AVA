#pragma once

#include <optional>
#include <string>

#include <nlohmann/json.hpp>

namespace ava::config {

enum class RoutingMode {
  Off,
  Conservative,
};

enum class RoutingProfile {
  Cheap,
  Capable,
};

struct RoutingTarget {
  std::optional<std::string> provider;
  std::optional<std::string> model;

  void normalize();
  [[nodiscard]] bool is_complete() const;
};

struct RoutingTargets {
  RoutingTarget cheap;
  RoutingTarget capable;
};

struct RoutingConfig {
  RoutingMode mode{RoutingMode::Off};
  RoutingTargets targets;

  [[nodiscard]] bool is_enabled() const;
  void normalize();
  [[nodiscard]] const RoutingTarget* target_for(RoutingProfile profile) const;
};

void to_json(nlohmann::json& j, RoutingMode value);
void from_json(const nlohmann::json& j, RoutingMode& value);
void to_json(nlohmann::json& j, RoutingProfile value);
void from_json(const nlohmann::json& j, RoutingProfile& value);
void to_json(nlohmann::json& j, const RoutingTarget& value);
void from_json(const nlohmann::json& j, RoutingTarget& value);
void to_json(nlohmann::json& j, const RoutingTargets& value);
void from_json(const nlohmann::json& j, RoutingTargets& value);
void to_json(nlohmann::json& j, const RoutingConfig& value);
void from_json(const nlohmann::json& j, RoutingConfig& value);

}  // namespace ava::config
