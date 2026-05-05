#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "ava/config/model_config_support.h"
#include "tests/support/test_harness.h"

namespace {

std::filesystem::path fresh_root(std::string name)
{
  auto root = temp_root() / std::move(name);
  std::error_code remove_error;
  std::filesystem::remove_all(root, remove_error);
  std::filesystem::create_directories(root);
  return root;
}

void write_text(std::filesystem::path const& path, std::string const& text)
{
  std::filesystem::create_directories(path.parent_path());
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  file << text;
}

bool near(long double value, long double expected)
{
  auto const delta = value > expected ? value - expected : expected - value;
  return delta < 0.0000001L;
}

void test_bounded_model_config_reads()
{
  auto const root = fresh_root("model-config-support-reads");
  auto const file_path = root / "models.json";
  write_text(file_path, R"({"models":[]})");

  auto content = ava::config::detail::read_model_config_text(file_path);
  expect(content && *content == R"({"models":[]})", "model config support reads regular bounded config files");

  auto const directory_path = root / "directory";
  std::filesystem::create_directories(directory_path);
  auto directory = ava::config::detail::read_model_config_text(directory_path);
  expect(!directory && directory.error().category() == ava::core::ErrorCategory::Io,
         "model config support rejects non-regular config paths");

  auto const large_path = root / "large.json";
  write_text(large_path, std::string(ava::config::detail::max_model_config_bytes + 1, 'x'));
  auto large = ava::config::detail::read_model_config_text(large_path);
  expect(!large && large.error().message().find("too large") != std::string::npos,
         "model config support rejects oversized config files");
}

void test_model_family_and_scalar_fields()
{
  expect(ava::config::detail::family_from_model_id("gpt-5.5") == "gpt-5" &&
             ava::config::detail::family_from_model_id("gpt-5-mini") == "gpt-5" &&
             ava::config::detail::family_from_model_id("custom-alpha-2026") == "custom-alpha" &&
             ava::config::detail::family_from_model_id("local") == "local",
         "model config support derives prompt families from model identifiers");

  auto const object = R"({"price":1.25e2,"bad":1.2x,"negative":-1,"zero":0,"count":42,"enabled":true,"broken":trueish})";
  auto price = ava::config::detail::number_field(object, "price");
  expect(price && near(*price, 125.0L), "model config support parses JSON number fields with exponents");
  expect(!ava::config::detail::number_field(object, "bad"),
         "model config support rejects numbers followed by non-delimiters");
  expect(ava::config::detail::first_number_field(object, {"negative", "price"}) == price,
         "model config support ignores negative pricing aliases before later valid aliases");
  expect(ava::config::detail::positive_integer_field(object, {"zero", "count"}).value_or(0) == 42,
         "model config support selects positive integer aliases");
  expect(ava::config::detail::bool_field(object, {"enabled"}) == true &&
             !ava::config::detail::bool_field(object, {"broken"}),
         "model config support parses strict JSON booleans");
  expect(ava::config::detail::has_any_field(object, {"missing", "count"}),
         "model config support detects any requested field");
}

void test_string_arrays_and_pricing()
{
  auto const object = R"({"input":["text",{"nested":["ignore"]},"image"],"empty":[]})";
  auto values = ava::config::detail::string_array_field(object, {"missing", "input"});
  expect(values.size() == 2 && values[0] == "text" && values[1] == "image",
         "model config support extracts only top-level strings from array fields");
  expect(ava::config::detail::string_array_field(object, {"empty"}).empty(),
         "model config support returns empty vectors for empty arrays");

  auto const priced =
      R"({"pricing":{"input_usd_per_1m":1.5,"output_per_million":2.5,"cache_read_usd_per_1m":0.25}})";
  auto pricing = ava::config::detail::model_pricing_from_item(priced);
  expect(pricing && pricing->input_per_million && near(*pricing->input_per_million, 1.5L) &&
             pricing->output_per_million && near(*pricing->output_per_million, 2.5L) &&
             pricing->cache_read_per_million && near(*pricing->cache_read_per_million, 0.25L),
         "model config support extracts nested pricing aliases");
  expect(!ava::config::detail::pricing_from_object(R"({"name":"free"})"),
         "model config support leaves pricing absent when no rates are provided");
}

void test_billable_usage_costs()
{
  ava::config::ModelPricing pricing{.input_per_million = 1.0L,
                                    .output_per_million = 2.0L,
                                    .cache_read_per_million = 0.1L,
                                    .cache_write_per_million = 0.2L,
                                    .reasoning_per_million = 3.0L};
  ava::provider::TokenUsage usage{.input_tokens = 1000,
                                  .output_tokens = 500,
                                  .reasoning_tokens = 50,
                                  .cache_read_tokens = 100,
                                  .cache_write_tokens = 200,
                                  .total_tokens = 1500,
                                  .estimated_input_bytes = std::nullopt,
                                  .estimated_output_bytes = std::nullopt,
                                  .estimated_total_bytes = std::nullopt,
                                  .estimated = false};
  auto cost = ava::config::detail::billable_usage_cost_usd(pricing, usage);
  expect(cost && near(*cost, 0.0018L),
         "model config support calculates regular, cached, and reasoning token costs");

  usage.estimated = true;
  expect(!ava::config::detail::billable_usage_cost_usd(pricing, usage),
         "model config support does not price estimated usage");

  usage.estimated = false;
  usage.output_tokens = std::nullopt;
  usage.total_tokens = 1500;
  auto fallback = ava::config::detail::billable_usage_cost_usd(pricing, usage);
  expect(fallback && near(*fallback, 0.0018L),
         "model config support derives output tokens from total tokens when needed");

  pricing.output_per_million = std::nullopt;
  expect(!ava::config::detail::billable_usage_cost_usd(pricing, usage),
         "model config support leaves cost unknown when required output pricing is absent");
}

}  // namespace

void run_config_model_config_support_tests()
{
  test_bounded_model_config_reads();
  test_model_family_and_scalar_fields();
  test_string_arrays_and_pricing();
  test_billable_usage_costs();
}
