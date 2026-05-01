#include "ava/config/model_config.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <initializer_list>
#include <optional>
#include <utility>

#include "ava/core/json.h"

namespace ava::config {
namespace {

constexpr std::size_t max_model_config_bytes = 1024 * 1024;

ava::core::Result<std::string> read_text(const std::filesystem::path& path) {
  std::error_code status_error;
  if (!std::filesystem::is_regular_file(path, status_error)) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "model config is not a regular file");
    error.with_context("path", path.string());
    if (status_error) error.with_context("cause", status_error.message());
    return std::unexpected(std::move(error));
  }
  std::error_code size_error;
  const auto size = std::filesystem::file_size(path, size_error);
  if (size_error || size > max_model_config_bytes) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "model config is too large");
    error.with_context("path", path.string());
    error.with_context("max_bytes", std::to_string(max_model_config_bytes));
    if (size_error) error.with_context("cause", size_error.message());
    return std::unexpected(std::move(error));
  }
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed to open model config");
    error.with_context("path", path.string());
    return std::unexpected(std::move(error));
  }
  std::string content;
  std::array<char, 4096> buffer{};
  while (file) {
    file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    if (file.gcount() > 0) content.append(buffer.data(), static_cast<std::size_t>(file.gcount()));
    if (content.size() > max_model_config_bytes) {
      auto error = ava::core::Error(ava::core::ErrorCategory::Io, "model config is too large");
      error.with_context("path", path.string());
      error.with_context("max_bytes", std::to_string(max_model_config_bytes));
      return std::unexpected(std::move(error));
    }
  }
  if (!file.eof() && file.fail()) {
    auto error = ava::core::Error(ava::core::ErrorCategory::Io, "failed while reading model config");
    error.with_context("path", path.string());
    return std::unexpected(std::move(error));
  }
  return content;
}

std::string family_from_model_id(std::string_view model_id) {
  if (model_id == "gpt-5" || model_id.starts_with("gpt-5.") || model_id.starts_with("gpt-5-")) return "gpt-5";
  const auto dash = model_id.find_last_of('-');
  if (dash == std::string_view::npos) return std::string(model_id);
  return std::string(model_id.substr(0, dash));
}

bool is_number_delimiter(char ch) {
  return ch == ',' || ch == '}' || ch == ']' || std::isspace(static_cast<unsigned char>(ch)) != 0;
}

std::optional<long double> number_field(std::string_view object, std::string_view key) {
  const auto start = ava::core::json::field_value_start(object, key);
  if (!start || *start >= object.size()) return std::nullopt;

  std::size_t index = *start;
  if (object[index] == '-') ++index;
  const auto digits_start = index;
  while (index < object.size() && std::isdigit(static_cast<unsigned char>(object[index])) != 0) ++index;
  if (index == digits_start) return std::nullopt;
  if (index < object.size() && object[index] == '.') {
    ++index;
    const auto fraction_start = index;
    while (index < object.size() && std::isdigit(static_cast<unsigned char>(object[index])) != 0) ++index;
    if (index == fraction_start) return std::nullopt;
  }
  if (index < object.size() && (object[index] == 'e' || object[index] == 'E')) {
    ++index;
    if (index < object.size() && (object[index] == '+' || object[index] == '-')) ++index;
    const auto exponent_start = index;
    while (index < object.size() && std::isdigit(static_cast<unsigned char>(object[index])) != 0) ++index;
    if (index == exponent_start) return std::nullopt;
  }
  if (index < object.size() && !is_number_delimiter(object[index])) return std::nullopt;

  try {
    return std::stold(std::string(object.substr(*start, index - *start)));
  } catch (...) {
    return std::nullopt;
  }
}

std::optional<long double> first_number_field(std::string_view object, std::initializer_list<std::string_view> keys) {
  for (const auto key : keys) {
    if (const auto value = number_field(object, key); value && *value >= 0.0L) return value;
  }
  return std::nullopt;
}

std::optional<long long> positive_integer_field(std::string_view object, std::initializer_list<std::string_view> keys) {
  for (const auto key : keys) {
    const auto value = ava::core::json::integer_field(object, key);
    if (value && *value > 0) return value;
  }
  return std::nullopt;
}

std::optional<ModelPricing> pricing_from_object(std::string_view object) {
  ModelPricing pricing;
  pricing.input_per_million = first_number_field(object, {"input_per_million", "input_usd_per_1m"});
  pricing.output_per_million = first_number_field(object, {"output_per_million", "output_usd_per_1m"});
  pricing.cache_read_per_million = first_number_field(object, {"cache_read_per_million", "cache_read_usd_per_1m"});
  pricing.cache_write_per_million = first_number_field(object, {"cache_write_per_million", "cache_write_usd_per_1m"});
  pricing.reasoning_per_million = first_number_field(object, {"reasoning_per_million", "reasoning_usd_per_1m"});
  if (!pricing.input_per_million && !pricing.output_per_million && !pricing.cache_read_per_million &&
      !pricing.cache_write_per_million && !pricing.reasoning_per_million) {
    return std::nullopt;
  }
  return pricing;
}

std::optional<ModelPricing> model_pricing_from_item(std::string_view item) {
  if (const auto object = ava::core::json::object_field(item, "pricing")) return pricing_from_object(*object);
  return pricing_from_object(item);
}

long double millionths(long long tokens, long double price_per_million) {
  return (static_cast<long double>(tokens) * price_per_million) / 1'000'000.0L;
}

}  // namespace

ModelRegistry builtin_model_registry() {
  return ModelRegistry{
      .default_provider_id = "openai",
      .default_model_id = "gpt-5.5",
      .models = {ModelInfo{.provider_id = "openai",
                           .model_id = "gpt-5.5",
                           .display_name = "GPT-5.5",
                           .family = "gpt-5",
                           .context_window_tokens = std::nullopt,
                           .max_output_tokens = std::nullopt,
                           .pricing = std::nullopt},
                 ModelInfo{.provider_id = "openai",
                           .model_id = "gpt-4.1-mini",
                           .display_name = "GPT-4.1 mini",
                           .family = "gpt-4.1",
                           .context_window_tokens = 1'048'576,
                           .max_output_tokens = 32'768,
                           .pricing = ModelPricing{.input_per_million = 0.40L,
                                                   .output_per_million = 1.60L,
                                                   .cache_read_per_million = 0.10L,
                                                   .cache_write_per_million = std::nullopt,
                                                   .reasoning_per_million = std::nullopt}}},
  };
}

ModelRegistry parse_model_registry(std::string_view content) {
  auto registry = builtin_model_registry();
  if (auto provider = ava::core::json::string_field(content, "default_provider"))
    registry.default_provider_id = *provider;
  if (auto model = ava::core::json::string_field(content, "default_model")) registry.default_model_id = *model;

  for (const auto& item : ava::core::json::objects_in_array_field(content, "models")) {
    auto provider = ava::core::json::string_field(item, "provider");
    auto id = ava::core::json::string_field(item, "id");
    if (!provider || !id) continue;
    registry.models.push_back(
        ModelInfo{.provider_id = *provider,
                  .model_id = *id,
                  .display_name = ava::core::json::string_field(item, "name").value_or(*id),
                  .family = ava::core::json::string_field(item, "family").value_or(family_from_model_id(*id)),
                  .context_window_tokens = positive_integer_field(item, {"context_window_tokens", "context_window"}),
                  .max_output_tokens = positive_integer_field(item, {"max_output_tokens"}),
                  .pricing = model_pricing_from_item(item)});
  }
  return registry;
}

ava::core::Result<ModelRegistry> load_model_registry(const XdgPaths& paths) {
  if (!std::filesystem::exists(paths.models_file)) return builtin_model_registry();
  auto content = read_text(paths.models_file);
  if (!content) return std::unexpected(content.error());
  return parse_model_registry(*content);
}

ModelInfo select_default_model(const ModelRegistry& registry) {
  for (const auto& model : registry.models) {
    if (model.provider_id == registry.default_provider_id && model.model_id == registry.default_model_id) return model;
  }
  return ModelInfo{
      .provider_id = registry.default_provider_id,
      .model_id = registry.default_model_id,
      .display_name = registry.default_model_id,
      .family = family_from_model_id(registry.default_model_id),
      .context_window_tokens = std::nullopt,
      .max_output_tokens = std::nullopt,
      .pricing = std::nullopt,
  };
}

std::optional<long double> usage_cost_usd(const ModelPricing& pricing, const ava::provider::TokenUsage& usage) {
  if (usage.estimated) return std::nullopt;

  const long long input_tokens = usage.input_tokens.value_or(0);
  const long long cache_read_tokens = usage.cache_read_tokens.value_or(0);
  const long long cache_write_tokens = usage.cache_write_tokens.value_or(0);
  if (cache_read_tokens > 0 && !pricing.cache_read_per_million) return std::nullopt;
  if (cache_write_tokens > 0 && !pricing.cache_write_per_million) return std::nullopt;
  long long regular_input_tokens = input_tokens;
  if (pricing.cache_read_per_million) regular_input_tokens -= std::min(regular_input_tokens, cache_read_tokens);
  if (pricing.cache_write_per_million) regular_input_tokens -= std::min(regular_input_tokens, cache_write_tokens);

  auto output_tokens = usage.output_tokens.or_else([&usage, input_tokens]() -> std::optional<long long> {
    if (!usage.total_tokens || *usage.total_tokens < input_tokens) return std::nullopt;
    return *usage.total_tokens - input_tokens;
  });
  const long long reasoning_tokens = usage.reasoning_tokens.value_or(0);
  long long regular_output_tokens = output_tokens.value_or(0);
  if (pricing.reasoning_per_million) regular_output_tokens -= std::min(regular_output_tokens, reasoning_tokens);

  if (regular_input_tokens > 0 && !pricing.input_per_million) return std::nullopt;
  if (regular_output_tokens > 0 && !pricing.output_per_million) return std::nullopt;

  long double total = 0.0L;
  bool has_billable_usage = false;
  if (regular_input_tokens > 0) {
    total += millionths(regular_input_tokens, *pricing.input_per_million);
    has_billable_usage = true;
  }
  if (pricing.cache_read_per_million && cache_read_tokens > 0) {
    total += millionths(cache_read_tokens, *pricing.cache_read_per_million);
    has_billable_usage = true;
  }
  if (pricing.cache_write_per_million && cache_write_tokens > 0) {
    total += millionths(cache_write_tokens, *pricing.cache_write_per_million);
    has_billable_usage = true;
  }
  if (regular_output_tokens > 0) {
    total += millionths(regular_output_tokens, *pricing.output_per_million);
    has_billable_usage = true;
  }
  if (pricing.reasoning_per_million && reasoning_tokens > 0) {
    total += millionths(reasoning_tokens, *pricing.reasoning_per_million);
    has_billable_usage = true;
  }

  return has_billable_usage ? std::optional<long double>(total) : std::nullopt;
}

}  // namespace ava::config
