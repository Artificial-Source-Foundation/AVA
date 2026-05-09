#include "ava/tools/lsp_tools.h"

#include <memory>
#include <utility>

namespace ava::tools {

ava::core::Result<LspDiagnosticsResult> lsp_diagnostics(ToolContext const& context, std::filesystem::path const& path)
{
  if (context.cancel_requested && context.cancel_requested())
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "tool canceled"));
  }
  if (auto permission = ensure_permission(context, ava::permissions::Operation::LspQuery, path, "", "lsp_diagnostics", "LSP diagnostics require permission");
      !permission)
  {
    return std::unexpected(std::move(permission.error()));
  }
  if (context.cancel_requested && context.cancel_requested())
  {
    return std::unexpected(ava::core::Error(ava::core::ErrorCategory::Unknown, "tool canceled"));
  }

  std::shared_ptr<ava::lsp::DiagnosticsProvider> provider = context.lsp_diagnostics_provider;
  if (!provider)
  {
    auto error = ava::core::Error(ava::core::ErrorCategory::Tool, "LSP diagnostics provider is unavailable");
    error.with_context("tool", "lsp_diagnostics");
    error.with_context("path", path.string());
    return std::unexpected(std::move(error));
  }

  auto diagnostics = provider->diagnostics(path, context.cancel_requested);
  if (!diagnostics)
    return std::unexpected(std::move(diagnostics.error()));
  return LspDiagnosticsResult{.path = path, .diagnostics = std::move(*diagnostics)};
}

}  // namespace ava::tools
