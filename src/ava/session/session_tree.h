#pragma once

#include "ava/session/session_metadata.h"
#include "ava/session/session_store.h"

#include "ava/core/result.h"

#include <filesystem>
#include <string>
#include <vector>

namespace ava::session {

struct SessionTreeNode
{
  SessionSummary summary;
  SessionMetadataView metadata;
  std::vector<std::string> children;
  bool current = false;
};

struct SessionTreeIndex
{
  std::string current_session_id;
  std::vector<SessionTreeNode> sessions;
  std::vector<std::string> roots;
  std::vector<std::string> leaves;
  std::vector<std::string> current_path;
};

[[nodiscard]] ava::core::Result<SessionTreeIndex> build_session_tree(
    std::filesystem::path const& workspace_dir, std::filesystem::path const& root_dir,
    std::string_view current_session_id = {});

}  // namespace ava::session
