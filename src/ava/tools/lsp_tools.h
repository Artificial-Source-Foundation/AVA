#pragma once

#include "ava/tools/file_tools.h"

#include "ava/lsp/lsp_client.h"

#include "ava/core/result.h"

#include <filesystem>
#include <vector>

namespace ava::tools {

struct LspDiagnosticsResult {
  std::filesystem::path path;
  std::vector<ava::lsp::Diagnostic> diagnostics;
};

struct LspSymbolsResult {
  std::filesystem::path path;
  std::string query;
  std::vector<ava::lsp::Symbol> symbols;
};

struct LspDefinitionResult {
  std::filesystem::path path;
  int line = 0;
  int column = 0;
  std::vector<ava::lsp::Location> locations;
};

[[nodiscard]] ava::core::Result<LspDiagnosticsResult> lsp_diagnostics(ToolContext const& context,
                                                                       std::filesystem::path const& path);
[[nodiscard]] ava::core::Result<LspSymbolsResult> lsp_document_symbols(ToolContext const& context,
                                                                       std::filesystem::path const& path);
[[nodiscard]] ava::core::Result<LspSymbolsResult> lsp_workspace_symbols(ToolContext const& context,
                                                                        std::string_view query);
[[nodiscard]] ava::core::Result<LspDefinitionResult> lsp_definition(ToolContext const& context,
                                                                     std::filesystem::path const& path, int line,
                                                                     int column);
[[nodiscard]] ava::core::Result<LspDefinitionResult> lsp_references(ToolContext const& context,
                                                                    std::filesystem::path const& path, int line,
                                                                    int column);

}  // namespace ava::tools
