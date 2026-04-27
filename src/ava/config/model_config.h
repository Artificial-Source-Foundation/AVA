#pragma once

#include <string>
#include <vector>

#include "ava/config/xdg_paths.h"
#include "ava/core/result.h"

namespace ava::config {

struct ModelInfo {
  std::string provider_id;
  std::string model_id;
  std::string display_name;
  std::string family;
};

struct ModelRegistry {
  std::string default_provider_id = "openai";
  std::string default_model_id = "gpt-5.5";
  std::vector<ModelInfo> models;
};

[[nodiscard]] ModelRegistry builtin_model_registry();
[[nodiscard]] ModelRegistry parse_model_registry(std::string_view content);
[[nodiscard]] ava::core::Result<ModelRegistry> load_model_registry(const XdgPaths& paths);
[[nodiscard]] ModelInfo select_default_model(const ModelRegistry& registry);

}  // namespace ava::config
