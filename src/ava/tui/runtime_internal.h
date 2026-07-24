#pragma once

#include "ava/tui/composer_internal.h"
#include "ava/tui/runtime.h"

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include "debug.h"

namespace ava::tui {

inline constexpr std::size_t kMaxTranscriptItems = 1000;

struct CappedTranscriptSnapshotUpdate
{
  std::size_t leading_evictions = 0;
  std::ptrdiff_t item_index_shift = 0;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

[[nodiscard]] CappedTranscriptSnapshotUpdate apply_capped_transcript_snapshot(std::vector<TranscriptItem>& destination,
                                                                              std::vector<TranscriptItem> const& submitted_transcript,
                                                                              std::vector<TranscriptItem> const& turn_transcript,
                                                                              std::size_t previous_leading_evictions);

// Toggles only the latest original card matching the established tool query.
// The returned transcript index lets the runtime preserve viewport anchoring.
[[nodiscard]] std::optional<std::size_t> toggle_latest_matching_tool_details(std::vector<TranscriptItem>& transcript, std::string_view query,
                                                                             bool global_details_visible);
[[nodiscard]] std::optional<std::string> parse_tui_tool_command_argument(std::string_view submitted);

enum class TuiActiveNonblockingCommandDispatchKind
{
  Unrecognized,
  Blocked,
  Handled,
};

// Internal active-run dispatch boundary. It keeps completion-state rejection
// ahead of the queue callback that can mutate active job state.
struct TuiActiveNonblockingCommandDispatchResult
{
  TuiActiveNonblockingCommandDispatchKind kind = TuiActiveNonblockingCommandDispatchKind::Unrecognized;
  std::string status = {};
  std::vector<std::string> output = {};

  AVA_DEBUG_PRINT_MEMBERS_ON
};

[[nodiscard]] std::optional<std::vector<std::string>> dispatch_tui_active_nonblocking_command(TuiActiveRunQueues const& queues, std::string const& submitted);
[[nodiscard]] TuiActiveNonblockingCommandDispatchResult dispatch_tui_active_nonblocking_command_gated(ComposerSnapshot const& completion_snapshot,
                                                                                                      TuiActiveRunQueues const& queues,
                                                                                                      std::string const& submitted);

// Clears a selector and paints truthful pending authority state before invoking
// a synchronous model/session callback. The callback remains authoritative;
// this boundary never applies or claims its result.
[[nodiscard]] ava::core::Result<TuiRuntimeStateSnapshot> dispatch_tui_selector_authority(
    ComposerSnapshot& snapshot, std::string pending_status, std::function<bool()> const& render,
    std::function<ava::core::Result<TuiRuntimeStateSnapshot>()> const& callback);

// Presentation-only state transitions used by the interactive reasoning-cycle path.
void apply_reasoning_cycle_success(ComposerSnapshot& snapshot, std::string feedback);
void clear_reasoning_feedback_for_user_input(ComposerSnapshot& snapshot);

}  // namespace ava::tui
