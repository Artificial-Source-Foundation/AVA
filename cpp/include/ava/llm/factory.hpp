#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "ava/config/credentials.hpp"
#include "ava/llm/provider.hpp"

namespace ava::llm {

[[nodiscard]] bool is_known_provider(std::string_view provider_name);
[[nodiscard]] std::optional<std::string> base_url_for_provider(std::string_view provider_name);

[[nodiscard]] ProviderPtr create_provider(
    const std::string& provider_name,
    const std::string& model,
    const ava::config::CredentialStore& credentials
);

[[nodiscard]] ProviderPtr create_mock_provider(const std::string& model, std::vector<std::string> responses);
[[nodiscard]] ProviderPtr create_mock_provider(const std::string& model, std::vector<LlmResponse> responses);

}  // namespace ava::llm
