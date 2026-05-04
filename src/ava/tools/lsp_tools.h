#pragma once

#include <filesystem>
#include <vector>

#include "ava/core/result.h"
#include "ava/lsp/lsp_client.h"
#include "ava/tools/file_tools.h"

namespace ava::tools {

struct LspDiagnosticsResult {
  std::filesystem::path path;
  std::vector<ava::lsp::Diagnostic> diagnostics;
};

[[nodiscard]] ava::core::Result<LspDiagnosticsResult> lsp_diagnostics(ToolContext const& context,
                                                                      std::filesystem::path const& path);

}  // namespace ava::tools
