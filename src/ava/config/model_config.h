#pragma once

#include "ava/config/xdg_paths.h"
#include "ava/provider/provider.h"
#include "ava/core/result.h"

#include <optional>
#include <string>
#include <vector>

namespace ava::config {

struct ModelPricing
{
  std::optional<long double> input_per_million;
  std::optional<long double> output_per_million;
  std::optional<long double> cache_read_per_million;
  std::optional<long double> cache_write_per_million;
  std::optional<long double> reasoning_per_million;
};

struct ModelInfo
{
  std::string provider_id;
  std::string model_id;
  std::string display_name;
  std::string family;
  std::optional<long long> context_window_tokens = std::nullopt;
  std::optional<long long> max_output_tokens = std::nullopt;
  std::optional<ModelPricing> pricing = std::nullopt;
  std::string api_family;
  std::vector<std::string> input_modalities;
  std::optional<bool> supports_tools = std::nullopt;
  std::optional<bool> supports_streaming = std::nullopt;
  std::optional<bool> supports_reasoning = std::nullopt;
  std::optional<bool> reports_usage = std::nullopt;
  std::vector<std::string> reasoning_levels;
  std::vector<std::string> compatibility_quirks;
  std::vector<std::string> output_modalities = {};
  std::string reasoning_format = {};
};

struct ModelRegistry
{
  std::string default_provider_id = "openai";
  std::string default_model_id = "gpt-5.5";
  std::vector<ModelInfo> models;
  std::optional<std::vector<std::string>> scoped_model_cycle = std::nullopt;
};

[[nodiscard]] ModelRegistry builtin_model_registry();
[[nodiscard]] ModelRegistry parse_model_registry(std::string_view content);
[[nodiscard]] ava::core::Result<ModelRegistry> load_model_registry(XdgPaths const& paths);
[[nodiscard]] ava::core::VoidResult store_scoped_model_cycle(XdgPaths const& paths,
                                                             std::optional<std::vector<std::string>> scoped_model_cycle);
[[nodiscard]] std::optional<ModelInfo> find_model(ModelRegistry const& registry, std::string_view provider_id, std::string_view model_id);
[[nodiscard]] ModelInfo select_default_model(ModelRegistry const& registry);
[[nodiscard]] std::optional<long double> usage_cost_usd(ModelPricing const& pricing, ava::provider::TokenUsage const& usage);

}  // namespace ava::config
