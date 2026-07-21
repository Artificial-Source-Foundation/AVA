#pragma once

#include "ava/config/xdg_paths.h"
#include "ava/session/session_store.h"
#include "ava/core/result.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ava::session {

struct CompactionConfig
{
  std::size_t auto_threshold_tokens = 0;
  bool auto_threshold_tokens_explicit = false;
  std::size_t keep_recent_tokens = 2048;
  std::size_t keep_recent_messages = 6;
  std::string model_id = "gpt-5.5";
  std::size_t max_summary_bytes = 16 * 1024;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct CompactionDecision
{
  bool should_compact = false;
  std::size_t estimated_tokens = 0;
  std::size_t threshold_tokens = 0;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct ManualCompactionRequest
{
  std::string summary;
  std::string instructions;
  CompactionConfig config;
  std::size_t estimated_tokens = 0;
  std::size_t threshold_tokens = 0;
  std::string trigger = "manual";
  std::string recent_context;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

[[nodiscard]] CompactionConfig default_compaction_config();
[[nodiscard]] ava::core::Result<CompactionConfig> parse_compaction_config(std::string_view content);
[[nodiscard]] ava::core::Result<CompactionConfig> load_compaction_config(ava::config::XdgPaths const& paths);
[[nodiscard]] std::size_t estimate_tokens(std::string_view text) noexcept;
// Estimates the shared logical/public projection, so committed v4 turns count
// while uncommitted staging and provider-private payloads never affect a
// compaction decision.
[[nodiscard]] ava::core::Result<std::size_t> estimate_session_tokens(std::vector<SessionEntry> const& entries);
[[nodiscard]] std::size_t effective_auto_threshold_tokens(CompactionConfig const& config, std::optional<long long> context_window_tokens) noexcept;
[[nodiscard]] ava::core::Result<CompactionDecision> should_auto_compact(std::vector<SessionEntry> const& entries, CompactionConfig const& config);
[[nodiscard]] ava::core::Result<CompactionDecision> should_auto_compact(std::vector<SessionEntry> const& entries, CompactionConfig const& config,
                                                                        std::optional<long long> context_window_tokens);
[[nodiscard]] ava::core::Result<SessionEntry> make_manual_compaction_entry(ManualCompactionRequest request);
[[nodiscard]] ava::core::VoidResult append_manual_compaction(SessionStore& store, SessionLease const& lease, ManualCompactionRequest request);
[[nodiscard]] ava::core::VoidResult append_manual_compaction_ephemeral(SessionStore& store, ManualCompactionRequest request);

}  // namespace ava::session
