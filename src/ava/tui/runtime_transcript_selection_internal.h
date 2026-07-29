#pragma once

#include "ava/debug/print_members_on.h"
#include "ava/tui/composer.h"
#include "ava/tui/composer_internal.h"
#include "ava/tui/runtime_draft_internal.h"
#include "ava/tui/terminal.h"

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ava::tui {

// Content-relative transcript selection endpoints.
//
// Origin contract:
// - `item_index` is the transcript item index owned by the authoritative
//   TranscriptLayout entry (`message_item_indices`).
// - `line_offset` is measured from that item's `message_starts` row in the same
//   layout (the first layout line belonging to the item). Context-group headings,
//   tool/thinking headers, markdown/code/tool body rows, and blank spacer rows
//   that belong to the item are included. Inter-item roomy spacers that sit
//   *before* `message_starts` are unowned and cannot host endpoints.
// - `display_column` is the cluster-aligned plain display column within that
//   rendered row after stripping SGR/OSC/control sequences (0 = first plain
//   column). Wide glyphs snap to cluster boundaries (half-column hit chooses the
//   nearer edge). Combining marks, ZWJ emoji, and regional-indicator pairs use
//   the same compact terminal cluster rules as rendering.
//
// Selection copies authoritative rendered PLAIN transcript rows (SGR/OSC/control
// stripped). Soft-wrapped rows join with '\n'. Existing `/copy*` paths remain
// semantic/full-fidelity and are intentionally separate.
struct TranscriptSelectionEndpoint
{
  std::size_t item_index = 0;
  std::size_t line_offset = 0;
  std::size_t display_column = 0;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct TranscriptSelectionRange
{
  TranscriptSelectionEndpoint anchor = {};
  TranscriptSelectionEndpoint focus = {};

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct TranscriptSelectionExtractResult
{
  std::string text = {};
  // True when construction stopped after observing more than the OSC52 ceiling
  // (kMaxTerminalClipboardTextBytes + 1). Callers must not copy oversize text.
  bool oversize = false;
  std::size_t examined_rows = 0;

  // Contains selected transcript text and must never be debug-streamed.
  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

struct TranscriptSelectionHit
{
  TranscriptSelectionEndpoint endpoint = {};
  std::size_t absolute_line = 0;
  bool on_header = false;
  bool tool_header = false;
  bool thinking_header = false;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

struct TranscriptSelectionViewport
{
  std::size_t transcript_height = 0;
  std::size_t content_width = 0;
  std::size_t canvas_left = 0;
  std::size_t max_scroll_offset = 0;
  std::size_t scroll_offset = 0;
  std::size_t visible_start = 0;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

// Pure geometry/extract helpers (layout-authority only; no snapshot mutation).
[[nodiscard]] std::string transcript_selection_plain_row(std::string_view styled_line);
[[nodiscard]] std::size_t transcript_selection_plain_columns(std::string_view plain_row);
[[nodiscard]] std::size_t snap_display_column(std::string_view plain_row, std::size_t display_column, bool prefer_end_on_half = false);
[[nodiscard]] std::optional<std::size_t> absolute_line_for_endpoint(detail::TranscriptLayout const& layout, TranscriptSelectionEndpoint const& endpoint);
[[nodiscard]] std::optional<TranscriptSelectionEndpoint> endpoint_for_absolute_line(detail::TranscriptLayout const& layout, std::size_t absolute_line,
                                                                                    std::size_t display_column);
[[nodiscard]] bool endpoint_less(TranscriptSelectionEndpoint const& left, TranscriptSelectionEndpoint const& right, detail::TranscriptLayout const& layout);
[[nodiscard]] std::pair<TranscriptSelectionEndpoint, TranscriptSelectionEndpoint> ordered_endpoints(TranscriptSelectionRange const& range,
                                                                                                    detail::TranscriptLayout const& layout);
[[nodiscard]] TranscriptSelectionExtractResult extract_transcript_selection_text(detail::TranscriptLayout const& layout, TranscriptSelectionRange const& range,
                                                                                 std::size_t max_bytes);
[[nodiscard]] std::string apply_transcript_selection_highlight(std::string_view styled_line, std::size_t column_start, std::size_t column_end,
                                                               bool plain_output);
void apply_transcript_selection_overlay(std::vector<std::string>& visible_lines, detail::TranscriptLayout const& layout, TranscriptSelectionRange const& range,
                                        std::size_t visible_start, bool plain_output);

enum class TranscriptSelectionMouseResult
{
  Ignored,
  Handled,
  HandledNeedsRender,
};

// One narrow owner shared by idle and active-run. Draft and transcript
// selections are mutually exclusive: starting one clears the other at the call
// site.
class RuntimeTranscriptSelectionState final
{
 public:
  void clear() noexcept;
  [[nodiscard]] bool empty() const noexcept;
  [[nodiscard]] bool dragging() const noexcept;
  [[nodiscard]] std::optional<TranscriptSelectionRange> range() const noexcept;
  void publish(ComposerSnapshot& snapshot) const;

  // Bind or rebind the layout authority currently drawn. Clears fail-closed when
  // the prior selection cannot be remapped into the new authority.
  void rebind_authority(detail::TranscriptLayout const& layout, std::size_t layout_generation, std::size_t width, ToolPresentation tool_presentation,
                        bool thinking_visible, bool compact_spacing, ComposerSnapshot const* snapshot = nullptr);
  void apply_item_index_shift(std::ptrdiff_t item_index_shift, detail::TranscriptLayout const& layout);
  void remap_or_clear(detail::TranscriptLayout const& layout);

  // Adopt only the renderer-owned cache that was actually drawn. This never
  // refreshes or mutates the cache; invalid authority fails closed.
  [[nodiscard]] bool ensure_authority(detail::TranscriptLayoutCache const& layout_cache, ComposerSnapshot const* snapshot = nullptr);

  [[nodiscard]] TranscriptSelectionMouseResult handle_mouse(InputEvent const& event, ComposerSnapshot& snapshot,
                                                            detail::TranscriptLayoutCache const& layout_cache, RuntimeDraftState* draft_state,
                                                            std::size_t& transcript_scroll_offset, std::function<bool(std::size_t)> const& toggle_tool,
                                                            std::function<bool(std::size_t)> const& toggle_thinking);

  [[nodiscard]] bool copy_selection(ComposerSnapshot& snapshot, detail::TranscriptLayoutCache const& layout_cache);

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT

 private:
  struct ItemSourceAuthority
  {
    std::string label = {};
    std::string text = {};
    std::string meta = {};
    std::string thinking = {};
    std::string stream_id = {};
    bool append_only_stream = false;
    bool tool = false;
    std::string tool_name = {};
    std::string tool_call_id = {};
    std::string tool_request_id = {};
    std::string tool_correlation_id = {};
    std::string tool_arguments = {};

    AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
  };

  enum class DragKind
  {
    None,
    Selecting,
    HeaderArmed,
  };

  [[nodiscard]] bool has_compatible_authority(detail::TranscriptLayoutCache const& cache) const noexcept;
  [[nodiscard]] std::optional<TranscriptSelectionViewport> viewport_for(ComposerSnapshot const& snapshot, detail::TranscriptLayoutCache const& cache) const;
  [[nodiscard]] std::optional<TranscriptSelectionHit> hit_test(ComposerSnapshot const& snapshot, detail::TranscriptLayoutCache const& cache, std::size_t row,
                                                               std::size_t column) const;
  [[nodiscard]] static std::optional<ItemSourceAuthority> source_authority(ComposerSnapshot const& snapshot, std::size_t item_index);
  [[nodiscard]] static bool source_authority_compatible(ItemSourceAuthority const& previous, ItemSourceAuthority const& current);
  [[nodiscard]] bool refresh_source_authorities_or_clear(ComposerSnapshot const& snapshot);
  void begin_selection(TranscriptSelectionEndpoint const& endpoint, ComposerSnapshot const& snapshot, RuntimeDraftState* draft_state);
  void extend_selection(TranscriptSelectionEndpoint const& endpoint, ComposerSnapshot const& snapshot);
  void arm_header(TranscriptSelectionEndpoint endpoint, bool tool_header, bool thinking_header);
  [[nodiscard]] bool finish_header_click(std::function<bool(std::size_t)> const& toggle_tool, std::function<bool(std::size_t)> const& toggle_thinking);
  [[nodiscard]] bool autoscroll_for_row(ComposerSnapshot& snapshot, TranscriptSelectionViewport const& viewport, std::size_t screen_row,
                                        std::size_t& transcript_scroll_offset);

  std::optional<TranscriptSelectionRange> range_ = std::nullopt;
  std::optional<ItemSourceAuthority> anchor_source_authority_ = std::nullopt;
  std::optional<ItemSourceAuthority> focus_source_authority_ = std::nullopt;
  DragKind drag_ = DragKind::None;
  std::optional<std::size_t> armed_header_item_ = std::nullopt;
  std::optional<TranscriptSelectionEndpoint> armed_press_endpoint_ = std::nullopt;
  bool armed_tool_header_ = false;
  bool armed_thinking_header_ = false;
  std::size_t authority_generation_ = 0;
  std::size_t authority_width_ = 0;
  ToolPresentation authority_tool_presentation_ = ToolPresentation::Rich;
  bool authority_thinking_visible_ = true;
  bool authority_compact_spacing_ = false;
  bool authority_valid_ = false;
};

}  // namespace ava::tui
