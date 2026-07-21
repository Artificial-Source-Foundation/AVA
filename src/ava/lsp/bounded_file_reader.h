#pragma once

#include "ava/debug/print_members_on.h"
#include "ava/core/result.h"

#include <chrono>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace ava::lsp {

// Workspace reads are descriptor-anchored below workspace_root. External reads
// intentionally leave absolute global configuration paths outside that anchor.
enum class BoundedFileReadScope
{
  External,
  Workspace,
};

// ForceComponentFallback is retained as a deterministic compatibility seam for
// tests and for kernels that do not implement openat2(2).
enum class BoundedFileOpenStrategy
{
  Automatic,
  ForceComponentFallback,
};

struct BoundedFileReadOptions
{
  std::filesystem::path path;
  std::filesystem::path workspace_root;
  std::size_t max_bytes = 0;
  BoundedFileReadScope scope = BoundedFileReadScope::External;
  bool missing_ok = false;
  bool metadata_only = false;
  bool require_private_owner = false;
  std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max();
  std::function<bool()> cancel_requested = nullptr;
  BoundedFileOpenStrategy open_strategy = BoundedFileOpenStrategy::Automatic;
  // Test-only deterministic seam: the pathname may be replaced after its
  // descriptor is opened to prove that subsequent bytes come from that fd.
  std::function<void()> after_open_for_testing = nullptr;

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

// Opens exactly one final descriptor, verifies its type and size before any
// content allocation, and reads only through that descriptor. A missing file is
// represented by nullopt only when missing_ok is set.
[[nodiscard]] ava::core::Result<std::optional<std::string>> read_bounded_lsp_file(BoundedFileReadOptions const& options);

}  // namespace ava::lsp
