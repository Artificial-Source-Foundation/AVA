#pragma once

#include "ava/session/record.h"
#include "ava/provider/provider.h"
#include "ava/core/result.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include "debug.h"

namespace ava::agent {

enum class HistoryReplayMode
{
  Automatic,
  ForcePortable,
};

struct HistoricalImagePolicy
{
  bool supports_images = false;
  ava::provider::ImageInputPolicy limits = {};

  [[nodiscard]] bool supports_mime_type(std::string_view mime_type) const noexcept;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct HistoryReplayTarget
{
  std::string provider_id;
  std::string model_id;
  std::string api_family;
  std::string reasoning_format;
  bool supports_tools = false;
  bool supports_images = false;

  [[nodiscard]] bool is_complete() const noexcept;
  [[nodiscard]] HistoricalImagePolicy image_policy() const;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct MessageBuildOptions
{
  std::size_t max_tool_result_context_bytes = 8 * 1024;
  std::optional<HistoryReplayTarget> target = std::nullopt;
  HistoryReplayMode replay_mode = HistoryReplayMode::Automatic;
  std::vector<std::string> active_turn_user_entry_ids = {};

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct HistoryProjection
{
  std::vector<ava::provider::ChatMessage> messages;
  bool used_compaction = false;

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

// Strictly classifies physical v4 history before producing a request-owned
// copy. A missing or incomplete target is always equivalent to ForcePortable.
[[nodiscard]] ava::core::Result<HistoryProjection> project_history_for_request(std::vector<ava::session::SessionEntry> const& entries,
                                                                               MessageBuildOptions const& options = {});

}  // namespace ava::agent
