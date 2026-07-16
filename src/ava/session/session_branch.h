#pragma once

#include "ava/session/session_metadata.h"
#include "ava/session/session_store.h"
#include "ava/core/result.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace ava::session {

enum class SessionBranchMode
{
  Fork,
  Clone
};

struct SessionBranchOptions
{
  std::filesystem::path workspace_dir;
  std::filesystem::path root_dir;
  std::string source_session_id;
  std::string branch_from_entry_id;
  std::optional<std::string> name;
  std::optional<std::vector<std::string>> labels;
  std::optional<SessionReadLimits> read_limits = std::nullopt;
  SessionBranchMode mode = SessionBranchMode::Fork;
  std::string actor = "rpc";

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct SessionBranchResult
{
  SessionStore store;
  SessionLease lease;
  std::string source_session_id;
  std::string branch_from_entry_id;
  std::size_t copied_entry_count = 0;
  SessionMetadataView metadata;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct BranchSummaryOptions
{
  std::filesystem::path workspace_dir;
  std::filesystem::path root_dir;
  std::string source_session_id;
  std::string branch_root_entry_id;
  std::string branch_tip_entry_id;
  std::string summary;
  std::string provider;
  std::string model;
  std::string reason;
  std::string actor = "rpc";

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct BranchSummaryResult
{
  std::string source_session_id;
  SessionEntry entry;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

// Roll back only a newly-created session JSONL file while preserving any attachment tree in place.
// The primary error remains authoritative; rollback observations are appended as context.
void rollback_created_session_with_context(SessionStore const& store, SessionLease const& lease, ava::core::Error& error);

[[nodiscard]] ava::core::Result<SessionBranchResult> create_session_branch(SessionBranchOptions options);
[[nodiscard]] ava::core::Result<BranchSummaryResult> append_branch_summary(BranchSummaryOptions options);

}  // namespace ava::session
