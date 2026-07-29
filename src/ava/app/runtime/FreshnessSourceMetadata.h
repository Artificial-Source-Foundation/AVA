#pragma once

#include "ava/debug/print_members_on.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>

namespace ava::app::runtime {

// Identify which subsystem produced a freshness source entry during prompt assembly.
enum class FreshnessSourceKind
{
  SystemPrompt,
  AppendSystemPrompt,
  PromptCommand,
  Skill,
  PluginManifest,
  PluginPrompt,
  PluginSkill,
};

[[nodiscard]] std::string to_string(runtime::FreshnessSourceKind kind);

// Record one tracked freshness source: its producer kind, human-readable scope/name and a content fingerprint for change detection.
struct FreshnessSourceMetadata
{
  FreshnessSourceKind kind = FreshnessSourceKind::Skill;
  std::string scope;
  std::string source_id;
  std::string name;
  std::filesystem::path path;
  std::size_t byte_count = 0;
  std::uint64_t content_fingerprint = 0;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

}  // namespace ava::app::runtime
