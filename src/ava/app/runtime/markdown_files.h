#pragma once

#include "ava/app/command_registry.h"

#include <filesystem>
#include <vector>

namespace ava::app::runtime {

[[nodiscard]] std::vector<std::filesystem::path> markdown_files(std::filesystem::path const& root, std::vector<CommandRegistryDiagnostic>& diagnostics,
                                                                UnifiedCommandSource source);

}  // namespace ava::app::runtime
