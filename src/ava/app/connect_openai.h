#pragma once

#include <iosfwd>
#include <optional>
#include <string>

#include "ava/config/xdg_paths.h"

namespace ava::app {

enum class ConnectCredentialType {
  ApiKey,
  OAuthToken,
};

struct ConnectProviderCredentialOptions {
  std::string provider_id;
  ConnectCredentialType credential_type = ConnectCredentialType::ApiKey;
  std::optional<std::string> env_var = std::nullopt;
};

struct ConnectProviderWizardOptions {
  std::optional<std::string> provider_id = std::nullopt;
  std::optional<ConnectCredentialType> credential_type = std::nullopt;
  bool stdin_is_tty = false;
};

[[nodiscard]] int run_connect_openai(const ava::config::XdgPaths& paths);
[[nodiscard]] int run_connect_provider_wizard(const ava::config::XdgPaths& paths,
                                              const ConnectProviderWizardOptions& options,
                                              std::istream& in,
                                              std::ostream& out,
                                              std::ostream& err);
[[nodiscard]] int run_connect_provider_credential(const ava::config::XdgPaths& paths,
                                                   const ConnectProviderCredentialOptions& options,
                                                  std::istream& in,
                                                  std::ostream& out,
                                                  std::ostream& err);

}  // namespace ava::app
