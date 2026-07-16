#pragma once

#include "ava/debug/print_members_on.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>

namespace ava::app::runtime {

// Track where the active base prompt came from and how large it is, including a fingerprint for staleness checks.
struct RuntimeBasePromptMetadata
{
  bool from_override = false;
  std::optional<std::filesystem::path> source_path = std::nullopt;
  std::size_t byte_count = 0;
  std::uint64_t content_fingerprint = 0;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

}  // namespace ava::app::runtime
