#pragma once

#include "ava/debug/print_members_on.h"
#include "ava/session/session_store.h"
#include "ava/core/result.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace ava::agent {

// Hard inspector caps applied on every coordinated live/final projection.
// Effective SessionReadLimits are still per-field min(authority policy, these).
inline constexpr std::size_t kSubagentInspectorMaxEntries = 1000;
inline constexpr std::size_t kSubagentInspectorMaxFileBytes = 1U * 1024U * 1024U;
inline constexpr std::size_t kSubagentInspectorMaxLineBytes = 256U * 1024U;
inline constexpr std::size_t kSubagentInspectorMaxMessages = 256;
inline constexpr std::size_t kSubagentInspectorMaxTextBytes = 256U * 1024U;
inline constexpr std::size_t kSubagentInspectorMaxItemTextBytes = 64U * 1024U;

[[nodiscard]] inline ava::session::SessionReadLimits subagent_inspector_read_limits() noexcept
{
  return ava::session::SessionReadLimits{
      .max_file_bytes = kSubagentInspectorMaxFileBytes,
      .max_line_bytes = kSubagentInspectorMaxLineBytes,
      .max_entries = kSubagentInspectorMaxEntries,
  };
}

enum class SubagentLiveMessageRole
{
  User,
  Assistant,
};

[[nodiscard]] std::string_view to_string(SubagentLiveMessageRole role) noexcept;

// Path-free committed public transcript item. MVP projects only ordinary
// user/assistant message text; tools and reasoning are intentionally absent.
struct SubagentLiveMessage
{
  SubagentLiveMessageRole role = SubagentLiveMessageRole::User;
  std::string text;

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

struct SubagentInspectorFrame
{
  std::uint64_t generation = 0;
  bool not_modified = false;
  bool terminal = false;
  bool freeze_pending = false;
  // Stable when a job was published without an inspection source (legacy/tests).
  bool unavailable = false;
  bool truncated = false;
  std::vector<SubagentLiveMessage> messages;

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

// Opaque lease-bound inspection source. Constructed from a COPY of an existing
// child SessionReadAuthority so no pathname or prompt escapes agent internals.
class SubagentLiveInspectionSource final
{
 public:
  ~SubagentLiveInspectionSource();

  SubagentLiveInspectionSource(SubagentLiveInspectionSource const&) = delete;
  SubagentLiveInspectionSource& operator=(SubagentLiveInspectionSource const&) = delete;
  SubagentLiveInspectionSource(SubagentLiveInspectionSource&&) = delete;
  SubagentLiveInspectionSource& operator=(SubagentLiveInspectionSource&&) = delete;

  [[nodiscard]] static ava::core::Result<std::shared_ptr<SubagentLiveInspectionSource>> create(ava::session::SessionReadAuthority authority);

  [[nodiscard]] std::string const& session_id() const noexcept;
  [[nodiscard]] bool is_ephemeral() const noexcept;
  [[nodiscard]] ava::core::Result<ava::session::SessionContentFingerprint> content_fingerprint() const;
  // Strict capped load: clamps inspector request limits under the authority
  // policy and never repairs torn tails.
  [[nodiscard]] ava::core::Result<std::vector<ava::session::SessionEntry>> load_strict_capped() const;
  [[nodiscard]] ava::core::Result<std::shared_ptr<SubagentInspectorFrame const>> project(std::uint64_t generation, bool terminal,
                                                                                         bool freeze_pending) const;

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT

 private:
  struct Impl;
  explicit SubagentLiveInspectionSource(std::unique_ptr<Impl> impl);

  std::unique_ptr<Impl> impl_;
};

[[nodiscard]] std::shared_ptr<SubagentInspectorFrame const> make_unavailable_inspection_frame(std::uint64_t generation, bool terminal,
                                                                                             bool freeze_pending = false);
[[nodiscard]] std::shared_ptr<SubagentInspectorFrame const> make_not_modified_inspection_frame(std::uint64_t generation, bool terminal,
                                                                                              bool freeze_pending = false);

}  // namespace ava::agent
