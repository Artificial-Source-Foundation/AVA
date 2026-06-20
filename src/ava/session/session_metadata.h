#pragma once

#include "ava/session/session_store.h"

#include "ava/core/result.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ava::session {

inline constexpr std::size_t kMaxSessionNameBytes = 256;
inline constexpr std::size_t kMaxSessionLabels = 32;
inline constexpr std::size_t kMaxSessionLabelBytes = 64;

struct SessionMetadataView
{
  std::string name;
  std::vector<std::string> labels;
  bool archived = false;
  std::string parent_session_id;
  std::string source_session_id;
  std::string branch_from_entry_id;
  std::string branch_origin;
  std::string actor;
};

struct SessionMetadataUpdate
{
  std::optional<std::string> name;
  std::optional<std::vector<std::string>> labels;
  std::optional<bool> archived;
  std::string parent_session_id;
  std::string source_session_id;
  std::string branch_from_entry_id;
  std::string branch_origin;
  std::string actor = "rpc";
};

[[nodiscard]] ava::core::Result<SessionMetadataView> session_metadata_from_entries(std::vector<SessionEntry> const& entries);
[[nodiscard]] ava::core::Result<SessionMetadataView> load_session_metadata(SessionStore const& store);
[[nodiscard]] ava::core::Result<SessionEntry> make_session_metadata_entry(SessionMetadataUpdate update, std::string parent_entry_id = {});
[[nodiscard]] ava::core::Result<SessionMetadataView> append_session_metadata(SessionStore& store, SessionMetadataUpdate update);

[[nodiscard]] std::string session_metadata_json(std::string_view session_id, SessionMetadataView const& metadata);

}  // namespace ava::session
