#pragma once

#include <memory>
#include <optional>
#include <string>

#include "ava/config/credentials.hpp"
#include "ava/llm/provider.hpp"
#include "ava/types/session.hpp"
#include "cli.hpp"

namespace ava::app {

struct ResolvedAgentSelection {
  std::string provider;
  std::string model;
  std::size_t max_turns{16};
};

[[nodiscard]] ResolvedAgentSelection resolve_agent_selection(
    const CliOptions& cli,
    const ava::types::SessionRecord& session
);

[[nodiscard]] ava::config::CredentialStore load_credentials_for_run();

[[nodiscard]] ava::llm::ProviderPtr build_provider_for_run(
    const ResolvedAgentSelection& selection,
    const ava::config::CredentialStore& credentials
);

}  // namespace ava::app
