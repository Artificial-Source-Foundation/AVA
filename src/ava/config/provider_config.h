#pragma once

#include "ava/debug/print_members_on.h"
#include "ava/config/xdg_paths.h"
#include "ava/core/result.h"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace ava::config {

// Wire protocol for a user-defined provider entry in providers.json.
// Distinct from ava::provider::ProviderProtocol, which classifies finish reasons.
enum class ProviderProtocol
{
  OpenAIChatCompletions,
  OpenAIResponses,
  AnthropicMessages,
};

// Credential-destination authority: where the runtime should look for secrets.
// This config never stores secret material itself.
enum class ProviderAuthMode
{
  ApiKey,
  None,
};

struct UserProviderCompatibility
{
  // Chat-completions only. Absent compatibility defaults to false.
  bool include_stream_usage = false;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

// Immutable user-authored provider definition after strict validation and defaulting.
// Not a mutable ProviderProfile and not registered into the runtime catalog here.
struct UserProviderDefinition
{
  std::string id;
  std::string display_name;
  ProviderProtocol protocol = ProviderProtocol::OpenAIChatCompletions;
  // base_url with trailing slashes removed; never contains userinfo/query/fragment.
  std::string base_url;
  // Absolute request path, either explicit or the protocol default.
  std::string request_path;
  // Canonical endpoint: base_url + request_path (exactly one slash join).
  std::string endpoint;
  ProviderAuthMode auth = ProviderAuthMode::ApiKey;
  // Populated for ApiKey (explicit or derived). Empty for None.
  std::string api_key_env;
  UserProviderCompatibility compatibility;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

inline constexpr std::size_t kMaxUserProviderConfigBytes = 256 * 1024;
inline constexpr std::size_t kMaxUserProviders = 128;

[[nodiscard]] std::string_view to_string(ProviderProtocol protocol) noexcept;
[[nodiscard]] std::string_view to_string(ProviderAuthMode auth) noexcept;
[[nodiscard]] std::string_view default_request_path_for(ProviderProtocol protocol) noexcept;
[[nodiscard]] std::string default_api_key_env_for_provider_id(std::string_view provider_id);

// Pure parse of one providers.json document body. Does not touch the filesystem.
[[nodiscard]] ava::core::Result<std::vector<UserProviderDefinition>> parse_user_provider_definitions(std::string_view content);

// Authority-sensitive load of paths.providers_file.
// Missing file => successful empty vector. Present unsafe/invalid file => failure (no fallback).
[[nodiscard]] ava::core::Result<std::vector<UserProviderDefinition>> load_user_provider_definitions(XdgPaths const& paths);

// Inert collision helper for Phase B catalog composition. Rejects any user id that
// appears in reserved_provider_ids (typically built-in provider ids). Duplicate
// user ids are already rejected by parse_user_provider_definitions.
[[nodiscard]] ava::core::VoidResult validate_user_provider_ids_against_reserved(std::span<UserProviderDefinition const> definitions,
                                                                                std::span<std::string_view const> reserved_provider_ids);

// Convenience wrapper over builtin_provider_profiles() ids. Remains inert until a
// catalog composer calls it.
[[nodiscard]] ava::core::VoidResult validate_user_provider_ids_against_builtins(std::span<UserProviderDefinition const> definitions);

}  // namespace ava::config
