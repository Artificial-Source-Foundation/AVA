#pragma once

#include <filesystem>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "ava/config/model_config.h"
#include "ava/core/result.h"
#include "ava/provider/provider.h"

namespace ava::config::detail {

inline constexpr std::size_t max_model_config_bytes = 1024 * 1024;

[[nodiscard]] ava::core::Result<std::string> read_model_config_text(std::filesystem::path const& path);
[[nodiscard]] std::string family_from_model_id(std::string_view model_id);
[[nodiscard]] bool is_json_scalar_delimiter(char ch);
[[nodiscard]] std::optional<long double> number_field(std::string_view object, std::string_view key);
[[nodiscard]] std::optional<long double> first_number_field(std::string_view object,
                                                            std::initializer_list<std::string_view> keys);
[[nodiscard]] std::optional<long long> positive_integer_field(std::string_view object,
                                                              std::initializer_list<std::string_view> keys);
[[nodiscard]] std::optional<bool> bool_field(std::string_view object,
                                             std::initializer_list<std::string_view> keys);
[[nodiscard]] bool has_any_field(std::string_view object, std::initializer_list<std::string_view> keys);
[[nodiscard]] std::vector<std::string> string_array_field(std::string_view object,
                                                          std::initializer_list<std::string_view> keys);
[[nodiscard]] std::optional<ModelPricing> pricing_from_object(std::string_view object);
[[nodiscard]] std::optional<ModelPricing> model_pricing_from_item(std::string_view item);
[[nodiscard]] long double millionths(long long tokens, long double price_per_million);
[[nodiscard]] std::optional<long double> billable_usage_cost_usd(ModelPricing const& pricing,
                                                                 ava::provider::TokenUsage const& usage);

}  // namespace ava::config::detail
