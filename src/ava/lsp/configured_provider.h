#pragma once

#include "ava/debug/print_members_on.h"
#include "ava/permissions/permission.h"
#include "ava/lsp/builtin_recipes.h"
#include "ava/lsp/lsp_client.h"
#include "ava/core/error.h"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace ava::lsp {

struct ConfiguredLspProviderFiles
{
  std::filesystem::path global_config_file;
  std::filesystem::path project_config_file;
  std::filesystem::path workspace_root;
  ava::agent::Mode mode = ava::agent::Mode::Build;
  ava::permissions::PermissionResolver permission_resolver = nullptr;
  BuiltinDiscoveryOptions builtin_discovery = {};

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct ConfiguredLspConfigDiagnostic
{
  std::string scope;
  std::filesystem::path path;
  bool exists = false;
  bool loaded = false;
  std::size_t byte_count = 0;
  std::size_t server_count = 0;
  std::optional<ava::core::Error> error = std::nullopt;
  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

struct ConfiguredLspProviderInspection
{
  std::vector<ConfiguredLspConfigDiagnostic> configs;
  std::vector<BuiltinServerInspection> builtin_servers;
  std::size_t server_count = 0;
  std::size_t error_count = 0;
  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

[[nodiscard]] ava::core::Result<std::shared_ptr<DiagnosticsProvider>> make_configured_lsp_provider(ConfiguredLspProviderFiles const& files);

// Parses configured global/project lsp.json files for user-facing diagnostics.
// This never starts configured servers or asks for launch permission.
[[nodiscard]] ConfiguredLspProviderInspection inspect_configured_lsp_provider(ConfiguredLspProviderFiles const& files);

// The configured provider protects its client cache, but individual cached LSP
// clients are still intended for AVA's serial tool-dispatch path.

}  // namespace ava::lsp
