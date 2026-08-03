#pragma once

#include "ava/debug/print_members_on.h"
#include "ava/config/model_config.h"
#include "ava/config/provider_profiles.h"
#include "ava/core/result.h"

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ava::config {

// Declarative first-class built-ins that ride the generic OpenAI Chat /
// Responses adapters. Source contracts were verified offline on 2026-08-03 from
// vendor public docs (no live API calls). Adding a provider here is the easy
// path: one spec row plus model rows — no bespoke factory copy/paste.

enum class BuiltinGenericProtocol
{
  OpenAIChatCompletions,
  OpenAIResponses,
};

enum class BuiltinMaxTokenField
{
  MaxTokens,            // chat max_tokens
  MaxCompletionTokens,  // chat max_completion_tokens (quirk)
  MaxOutputTokens,      // responses max_output_tokens
};

struct BuiltinGenericProviderSpec
{
  std::string_view provider_id = {};
  std::string_view display_name = {};
  std::string_view connect_detail = "API key";
  std::string_view api_key_env = {};
  std::string_view base_url_env = {};
  std::string_view default_base_url = {};
  std::string_view request_path = {};
  BuiltinGenericProtocol protocol = BuiltinGenericProtocol::OpenAIChatCompletions;
  BuiltinMaxTokenField max_token_field = BuiltinMaxTokenField::MaxTokens;
  bool include_stream_usage = false;
  // When true, profile advertises Responses-native reasoning levels.
  bool supports_reasoning = false;
  // Authority note for tests/docs (not user-facing).
  std::string_view source_note = {};

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct BuiltinGenericModelSpec
{
  std::string_view provider_id = {};
  std::string_view model_id = {};
  std::string_view display_name = {};
  std::string_view family = {};
  std::optional<long long> context_window_tokens = std::nullopt;
  std::optional<long long> max_output_tokens = std::nullopt;
  std::optional<long double> input_per_million = std::nullopt;
  std::optional<long double> output_per_million = std::nullopt;
  std::optional<long double> cache_read_per_million = std::nullopt;
  bool supports_tools = true;
  bool supports_images = false;
  bool supports_reasoning = false;
  // Empty unless supports_reasoning.
  std::span<char const* const> reasoning_levels = {};

  AVA_DEBUG_PRINT_MEMBERS_ON
};

// Catalog-time resolution of one generic built-in base URL + endpoint. Factories
// capture these exact strings; they must never re-read process environment.
struct ResolvedBuiltinGenericProvider
{
  std::string provider_id;
  std::string display_name;
  std::string base_url_env;
  // Canonical base (scheme://host[:port][/path...], no trailing slash).
  std::string base_url;
  // Absolute request path from the declarative spec.
  std::string request_path;
  // Exact request URL: base_url + request_path.
  std::string endpoint;
  BuiltinGenericProtocol protocol = BuiltinGenericProtocol::OpenAIChatCompletions;
  bool include_stream_usage = false;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

[[nodiscard]] std::span<BuiltinGenericProviderSpec const> builtin_generic_provider_specs() noexcept;
[[nodiscard]] std::span<BuiltinGenericModelSpec const> builtin_generic_model_specs() noexcept;

[[nodiscard]] ProviderProfile provider_profile_from_builtin_generic_spec(BuiltinGenericProviderSpec const& spec);
[[nodiscard]] std::vector<ProviderProfile> builtin_generic_provider_profiles();
[[nodiscard]] std::vector<ModelInfo> builtin_generic_model_infos();

[[nodiscard]] std::optional<BuiltinGenericProviderSpec> find_builtin_generic_provider_spec(std::string_view provider_id) noexcept;
[[nodiscard]] std::string builtin_generic_canonical_endpoint(BuiltinGenericProviderSpec const& spec);

// Resolve all declarative generic built-ins once.
// When read_base_url_env is true, nonempty <PROVIDER>_BASE_URL values are
// validated with the same remote-HTTPS / loopback-HTTP policy as providers.json.
// Invalid nonempty overrides fail closed with sanitized provider/env context
// (raw override values are never echoed). Missing/empty env uses the compiled
// default, which is also canonicalized.
[[nodiscard]] ava::core::Result<std::vector<ResolvedBuiltinGenericProvider>> resolve_builtin_generic_providers(bool read_base_url_env);

// Overlay catalog-time resolved base/endpoint onto a matching profile descriptor.
void apply_resolved_builtin_generic_to_profile(ProviderProfile& profile, ResolvedBuiltinGenericProvider const& resolved) noexcept;

}  // namespace ava::config
