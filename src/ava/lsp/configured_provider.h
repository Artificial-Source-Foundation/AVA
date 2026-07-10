#pragma once

#include "ava/lsp/lsp_client.h"

#include "ava/permissions/permission.h"

#include <filesystem>
#include <memory>

namespace ava::lsp {

struct ConfiguredLspProviderFiles {
  std::filesystem::path global_config_file;
  std::filesystem::path project_config_file;
  std::filesystem::path workspace_root;
  ava::agent::Mode mode = ava::agent::Mode::Build;
  ava::permissions::PermissionResolver permission_resolver = nullptr;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

[[nodiscard]] ava::core::Result<std::shared_ptr<DiagnosticsProvider>> make_configured_lsp_provider(
    ConfiguredLspProviderFiles const& files);

// The configured provider protects its client cache, but individual cached LSP
// clients are still intended for AVA's serial tool-dispatch path.

}  // namespace ava::lsp
