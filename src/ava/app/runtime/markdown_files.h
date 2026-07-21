#pragma once

#include "ava/app/command_registry.h"
#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace ava::app::runtime {

struct ParsedMarkdown
{
  std::map<std::string, std::string> frontmatter;
  std::string body;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

[[nodiscard]] ParsedMarkdown parse_markdown(std::string_view content);
[[nodiscard]] std::string markdown_field(ParsedMarkdown const& markdown, std::string_view name);
[[nodiscard]] std::vector<std::filesystem::path> markdown_files(std::filesystem::path const& root, std::vector<CommandRegistryDiagnostic>& diagnostics, UnifiedCommandSource source);

} // namespace ava::app::runtime
