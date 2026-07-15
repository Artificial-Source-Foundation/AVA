#pragma once

#include "ava/context/context_loader.h"
#include "ava/debug/print_members_on.h"

#include <cstddef>
#include <cstdint>
#include <filesystem>

namespace ava::app {

// Describe one file that contributed to the prompt context, with its origin type and a content fingerprint.
struct ContextSourceMetadata
{
  std::filesystem::path path;
  ava::context::ContextSourceType source_type = ava::context::ContextSourceType::Workspace;
  std::size_t byte_count = 0;
  std::uint64_t content_fingerprint = 0;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

}  // namespace ava::app
