#pragma once

#include "ava/debug/print_members_on.h"
#include "ava/core/result.h"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace ava::core {
class AnchorSet;
}

namespace ava::lsp {

// Workspace reads remain confined to workspace_root and are acquired through
// the caller's shared AnchorSet. External reads use the same AnchorSet so paths
// entering writable anchors cannot bypass descriptor-relative acquisition.
enum class BoundedFileReadScope
{
  External,
  Workspace,
};

struct BoundedFileReadOptions
{
  std::filesystem::path path;
  std::filesystem::path workspace_root;
  std::shared_ptr<ava::core::AnchorSet const> anchor_set = nullptr;
  std::size_t max_bytes = 0;
  BoundedFileReadScope scope = BoundedFileReadScope::External;
  bool missing_ok = false;
  bool metadata_only = false;
  bool require_private_owner = false;
  std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max();
  std::function<bool()> cancel_requested = nullptr;
  // Test-only deterministic seam: the pathname may be replaced after its
  // descriptor is opened to prove that subsequent bytes come from that fd.
  std::function<void()> after_open_for_testing = nullptr;

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

// Opens exactly one final descriptor through AnchorOpen, verifies its type and
// size before allocation, and reads only through that descriptor. A missing
// file is represented by nullopt only when missing_ok is set.
[[nodiscard]] ava::core::Result<std::optional<std::string>> read_bounded_lsp_file(BoundedFileReadOptions const& options);

}  // namespace ava::lsp
