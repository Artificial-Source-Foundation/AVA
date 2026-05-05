#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "ava/config/auth.h"
#include "ava/core/result.h"

namespace ava::config {

struct AuthMember {
  std::string key;
  std::string raw_value;
};

[[nodiscard]] ava::core::Result<std::vector<AuthMember>> parse_auth_members(std::string_view text,
                                                                            std::filesystem::path const& path);
[[nodiscard]] std::string render_auth_members(std::vector<AuthMember> const& members);
[[nodiscard]] ava::core::Result<std::string> provider_credential_object_json(ProviderCredential const& credential);

}  // namespace ava::config
