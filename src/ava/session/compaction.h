#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include "ava/config/xdg_paths.h"
#include "ava/core/result.h"
#include "ava/session/session_store.h"

namespace ava::session {

struct CompactionConfig {
  std::size_t auto_threshold_tokens = 0;
  std::size_t keep_recent_tokens = 2048;
  std::size_t keep_recent_messages = 8;
  std::string model_id = "gpt-5.5";
  std::size_t max_summary_bytes = 16 * 1024;
};

struct CompactionDecision {
  bool should_compact = false;
  std::size_t estimated_tokens = 0;
  std::size_t threshold_tokens = 0;
};

struct ManualCompactionRequest {
  std::string summary;
  std::string instructions;
  CompactionConfig config;
  std::size_t estimated_tokens = 0;
};

[[nodiscard]] CompactionConfig default_compaction_config();
[[nodiscard]] ava::core::Result<CompactionConfig> parse_compaction_config(std::string_view content);
[[nodiscard]] ava::core::Result<CompactionConfig> load_compaction_config(const ava::config::XdgPaths& paths);
[[nodiscard]] std::size_t estimate_tokens(std::string_view text) noexcept;
[[nodiscard]] std::size_t estimate_session_tokens(const std::vector<SessionEntry>& entries) noexcept;
[[nodiscard]] CompactionDecision should_auto_compact(const std::vector<SessionEntry>& entries,
                                                     const CompactionConfig& config) noexcept;
[[nodiscard]] ava::core::VoidResult append_manual_compaction(SessionStore& store,
                                                             ManualCompactionRequest request);

}  // namespace ava::session
