#include "sys.h"
#include "ava/tui/composer_internal.h"
#include "ava/tui/runtime_transcript_internal.h"
#include "ava/tui/runtime_transcript_selection_internal.h"
#include "ava/tui/theme.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <limits>
#include <utility>
#include <curses.h>

namespace ava::tui {
namespace {

constexpr std::size_t kSelectionCopySoftCeiling = runtime_transcript::kMaxTerminalClipboardTextBytes;

bool is_c0_or_del(unsigned char byte)
{
  return byte < 0x20U || byte == 0x7FU;
}

bool is_c1_control(char32_t codepoint)
{
  return codepoint >= 0x80 && codepoint <= 0x9F;
}

std::string strip_controls_keep_newline_tab(std::string_view text)
{
  std::string out;
  out.reserve(text.size());
  for (std::size_t index = 0; index < text.size();)
  {
    auto const before = index;
    if (detail::skip_sgr_sequence(text, index) || detail::skip_osc_sequence(text, index))
      continue;
    auto const byte = static_cast<unsigned char>(text[before]);
    if (byte == '\n' || byte == '\t')
    {
      out.push_back(static_cast<char>(byte));
      ++index;
      continue;
    }
    if (is_c0_or_del(byte))
    {
      ++index;
      continue;
    }
    auto const cell = detail::terminal_text_cell(text, before);
    if (!cell.valid)
    {
      ++index;
      continue;
    }
    char32_t codepoint = 0;
    auto const length = detail::utf8_sequence_length(byte);
    if (detail::decode_utf8_codepoint(text, before, length, codepoint) && is_c1_control(codepoint))
    {
      index = before + cell.bytes;
      continue;
    }
    out.append(text.substr(before, cell.bytes));
    index = before + cell.bytes;
  }
  return out;
}

std::size_t item_line_count(detail::TranscriptLayout const& layout, std::size_t position)
{
  if (position >= layout.message_starts.size() || position >= layout.block_boundaries.size())
    return 0;
  auto const start = layout.message_starts[position];
  // The next block boundary is recorded before the next item's roomy spacer,
  // so that spacer is unowned while internal blanks and headings stay owned.
  auto const end = position + 1 < layout.block_boundaries.size() ? layout.block_boundaries[position + 1] : layout.lines.size();
  return start <= end && end <= layout.lines.size() ? end - start : 0;
}

std::optional<std::size_t> position_for_item(detail::TranscriptLayout const& layout, std::size_t item_index)
{
  auto const it = std::ranges::find(layout.message_item_indices, item_index);
  if (it == layout.message_item_indices.end())
    return std::nullopt;
  return static_cast<std::size_t>(it - layout.message_item_indices.begin());
}

std::optional<TranscriptSelectionEndpoint> remap_endpoint(detail::TranscriptLayout const& layout, TranscriptSelectionEndpoint endpoint)
{
  auto const position = position_for_item(layout, endpoint.item_index);
  if (!position)
    return std::nullopt;
  auto const count = item_line_count(layout, *position);
  if (count == 0)
    return std::nullopt;
  endpoint.line_offset = std::min(endpoint.line_offset, count - 1);
  auto const absolute = layout.message_starts[*position] + endpoint.line_offset;
  if (absolute >= layout.lines.size())
    return std::nullopt;
  auto const plain = transcript_selection_plain_row(layout.lines[absolute]);
  endpoint.display_column = snap_display_column(plain, endpoint.display_column, false);
  endpoint.display_column = std::min(endpoint.display_column, transcript_selection_plain_columns(plain));
  return endpoint;
}

std::string plain_slice(std::string_view plain_row, std::size_t column_start, std::size_t column_end)
{
  if (column_start >= column_end)
    return {};
  std::string out;
  std::size_t column = 0;
  for (std::size_t index = 0; index < plain_row.size() && column < column_end;)
  {
    auto const cell = detail::terminal_text_cell(plain_row, index);
    auto const next_column = column + std::max<std::size_t>(cell.columns, 1);
    if (next_column > column_start && column < column_end)
      out.append(plain_row.substr(index, cell.bytes));
    index += std::max<std::size_t>(cell.bytes, 1);
    column = next_column;
  }
  return out;
}

bool append_bounded(std::string& out, std::string_view chunk, std::size_t max_bytes, bool& oversize)
{
  if (oversize)
    return false;
  if (chunk.empty())
    return true;
  if (out.size() >= max_bytes)
  {
    oversize = true;
    return false;
  }
  auto const remaining = max_bytes - out.size();
  if (chunk.size() > remaining)
  {
    // Stop at max_bytes+1 observation without keeping a truncated payload.
    oversize = true;
    out.clear();
    return false;
  }
  out.append(chunk);
  return true;
}

}  // namespace

std::string transcript_selection_plain_row(std::string_view styled_line)
{
  return strip_controls_keep_newline_tab(styled_line);
}

std::size_t transcript_selection_plain_columns(std::string_view plain_row)
{
  return detail::terminal_text_columns(plain_row);
}

std::size_t snap_display_column(std::string_view plain_row, std::size_t display_column, bool prefer_end_on_half)
{
  std::size_t column = 0;
  std::size_t last_boundary = 0;
  for (std::size_t index = 0; index < plain_row.size();)
  {
    auto const cell = detail::terminal_text_cell(plain_row, index);
    auto const columns = std::max<std::size_t>(cell.columns, 1);
    if (display_column <= column)
      return column;
    if (display_column < column + columns)
    {
      auto const into = display_column - column;
      if (prefer_end_on_half)
        return into * 2 >= columns ? column + columns : column;
      return into * 2 > columns ? column + columns : column;
    }
    index += std::max<std::size_t>(cell.bytes, 1);
    column += columns;
    last_boundary = column;
  }
  return std::min(display_column, last_boundary);
}

std::optional<std::size_t> absolute_line_for_endpoint(detail::TranscriptLayout const& layout, TranscriptSelectionEndpoint const& endpoint)
{
  auto const position = position_for_item(layout, endpoint.item_index);
  if (!position)
    return std::nullopt;
  auto const start = layout.message_starts[*position];
  auto const count = item_line_count(layout, *position);
  if (endpoint.line_offset >= count)
    return std::nullopt;
  auto const absolute = start + endpoint.line_offset;
  if (absolute >= layout.lines.size())
    return std::nullopt;
  return absolute;
}

std::optional<std::size_t> shift_transcript_selection_item_index(std::size_t item_index, std::ptrdiff_t item_index_shift) noexcept
{
  if (item_index_shift == 0)
    return item_index;
  if (item_index_shift > 0)
  {
    auto const amount = static_cast<std::size_t>(item_index_shift);
    if (item_index > std::numeric_limits<std::size_t>::max() - amount)
      return std::nullopt;
    return item_index + amount;
  }
  auto const amount = static_cast<std::size_t>(-(item_index_shift + 1)) + 1;
  if (item_index < amount)
    return std::nullopt;
  return item_index - amount;
}

std::optional<TranscriptSelectionEndpoint> endpoint_for_absolute_line(detail::TranscriptLayout const& layout, std::size_t absolute_line,
                                                                      std::size_t display_column)
{
  if (layout.message_starts.empty() || absolute_line >= layout.lines.size())
    return std::nullopt;
  // Zero-allocation upper_bound directly on the const vector authority (no copy).
  auto it = std::upper_bound(layout.message_starts.begin(), layout.message_starts.end(), absolute_line);
  if (it == layout.message_starts.begin())
    return std::nullopt;  // unowned leading spacer
  --it;
  auto const position = static_cast<std::size_t>(it - layout.message_starts.begin());
  if (position >= layout.message_item_indices.size())
    return std::nullopt;
  auto const start = layout.message_starts[position];
  auto const count = item_line_count(layout, position);
  if (absolute_line < start || absolute_line >= start + count)
    return std::nullopt;
  auto const plain = transcript_selection_plain_row(layout.lines[absolute_line]);
  auto const snapped = snap_display_column(plain, display_column, /*prefer_end_on_half=*/false);
  return TranscriptSelectionEndpoint{.item_index = layout.message_item_indices[position], .line_offset = absolute_line - start, .display_column = snapped};
}

bool endpoint_less(TranscriptSelectionEndpoint const& left, TranscriptSelectionEndpoint const& right, detail::TranscriptLayout const& layout)
{
  auto const left_abs = absolute_line_for_endpoint(layout, left);
  auto const right_abs = absolute_line_for_endpoint(layout, right);
  if (!left_abs || !right_abs)
    return false;
  if (*left_abs != *right_abs)
    return *left_abs < *right_abs;
  return left.display_column < right.display_column;
}

std::pair<TranscriptSelectionEndpoint, TranscriptSelectionEndpoint> ordered_endpoints(TranscriptSelectionRange const& range,
                                                                                      detail::TranscriptLayout const& layout)
{
  if (endpoint_less(range.focus, range.anchor, layout))
    return {range.focus, range.anchor};
  return {range.anchor, range.focus};
}

TranscriptSelectionExtractResult extract_transcript_selection_text(detail::TranscriptLayout const& layout, TranscriptSelectionRange const& range,
                                                                   std::size_t max_bytes)
{
  TranscriptSelectionExtractResult result;
  auto const ordered = ordered_endpoints(range, layout);
  auto const start_abs = absolute_line_for_endpoint(layout, ordered.first);
  auto const end_abs = absolute_line_for_endpoint(layout, ordered.second);
  if (!start_abs || !end_abs || *start_abs > *end_abs)
    return result;

  // Bound construction: stop once more than max_bytes would be required
  // (observe max_bytes+1 without retaining a truncated payload).
  auto const hard_limit = max_bytes;
  bool emitted_row = false;
  for (std::size_t line = *start_abs; line <= *end_abs; ++line)
  {
    ++result.examined_rows;
    if (line < layout.presentation_private_rows.size() && layout.presentation_private_rows[line])
      continue;
    auto const plain = transcript_selection_plain_row(layout.lines[line]);
    auto const line_columns = transcript_selection_plain_columns(plain);
    auto col_start = line == *start_abs ? ordered.first.display_column : 0;
    auto col_end = line == *end_abs ? ordered.second.display_column : line_columns;
    col_start = std::min(col_start, line_columns);
    col_end = std::min(col_end, line_columns);
    if (col_start > col_end)
      std::swap(col_start, col_end);
    auto slice = plain_slice(plain, col_start, col_end);
    if (emitted_row)
    {
      if (!append_bounded(result.text, "\n", hard_limit, result.oversize))
      {
        result.text.clear();
        return result;
      }
    }
    if (!append_bounded(result.text, slice, hard_limit, result.oversize))
    {
      result.text.clear();
      return result;
    }
    emitted_row = true;
  }
  if (result.text.empty() && !result.oversize)
  {
    // Empty selection (caret-only) is not copyable.
  }
  return result;
}

std::string apply_transcript_selection_highlight(std::string_view styled_line, std::size_t column_start, std::size_t column_end, bool plain_output)
{
  if (plain_output)
    return transcript_selection_plain_row(styled_line);
  if (column_start >= column_end)
    return std::string(styled_line);

  std::string out;
  out.reserve(styled_line.size() + 16);
  std::size_t column = 0;
  bool in_selection = false;
  auto open_selection = [&] {
    if (!in_selection)
    {
      out += detail::kReverseVideo;
      in_selection = true;
    }
  };
  auto close_selection = [&] {
    if (in_selection)
    {
      // SGR 27 disables reverse while preserving surrounding presentation.
      out += detail::kReverseVideoOff;
      in_selection = false;
    }
  };

  for (std::size_t index = 0; index < styled_line.size();)
  {
    auto before = index;
    if (detail::skip_sgr_sequence(styled_line, index) || detail::skip_osc_sequence(styled_line, index))
    {
      // Close reverse around foreign SGR so theme colors restore cleanly after.
      auto const was_selected = in_selection;
      close_selection();
      out.append(styled_line.substr(before, index - before));
      if (was_selected && column >= column_start && column < column_end)
        open_selection();
      continue;
    }
    auto const cell = detail::terminal_text_cell(styled_line, before);
    auto const columns = std::max<std::size_t>(cell.columns, 1);
    auto const next_column = column + columns;
    auto const selected = next_column > column_start && column < column_end;
    if (selected)
      open_selection();
    else
      close_selection();
    out.append(styled_line.substr(before, cell.bytes));
    index = before + std::max<std::size_t>(cell.bytes, 1);
    column = next_column;
  }
  close_selection();
  return out;
}

void apply_transcript_selection_overlay(std::vector<std::string>& visible_lines, detail::TranscriptLayout const& layout, TranscriptSelectionRange const& range,
                                        std::size_t visible_start, bool plain_output)
{
  if (visible_lines.empty())
    return;
  auto const ordered = ordered_endpoints(range, layout);
  auto const start_abs = absolute_line_for_endpoint(layout, ordered.first);
  auto const end_abs = absolute_line_for_endpoint(layout, ordered.second);
  if (!start_abs || !end_abs || *start_abs > *end_abs)
    return;

  for (std::size_t row = 0; row < visible_lines.size(); ++row)
  {
    auto const absolute = visible_start + row;
    if (absolute < *start_abs || absolute > *end_abs || absolute >= layout.lines.size())
      continue;
    auto const plain = transcript_selection_plain_row(layout.lines[absolute]);
    auto const line_columns = transcript_selection_plain_columns(plain);
    auto col_start = absolute == *start_abs ? ordered.first.display_column : 0;
    auto col_end = absolute == *end_abs ? ordered.second.display_column : line_columns;
    col_start = std::min(col_start, line_columns);
    col_end = std::min(col_end, line_columns);
    if (col_start >= col_end)
      continue;
    // Overlay mutates only the visible frame copy, never layout.lines.
    visible_lines[row] = apply_transcript_selection_highlight(visible_lines[row], col_start, col_end, plain_output);
  }
}

void RuntimeTranscriptSelectionState::clear() noexcept
{
  range_.reset();
  anchor_source_authority_.reset();
  focus_source_authority_.reset();
  drag_ = DragKind::None;
  armed_header_item_.reset();
  armed_press_endpoint_.reset();
  armed_source_authority_.reset();
  armed_tool_header_ = false;
  armed_thinking_header_ = false;
}

void RuntimeTranscriptSelectionState::cancel_pointer_interaction() noexcept
{
  // Preserve any committed range; only tear down in-flight press/drag/header arm.
  drag_ = DragKind::None;
  armed_header_item_.reset();
  armed_press_endpoint_.reset();
  armed_source_authority_.reset();
  armed_tool_header_ = false;
  armed_thinking_header_ = false;
}

bool RuntimeTranscriptSelectionState::empty() const noexcept
{
  return !range_;
}

bool RuntimeTranscriptSelectionState::dragging() const noexcept
{
  return drag_ != DragKind::None;
}

std::optional<TranscriptSelectionRange> RuntimeTranscriptSelectionState::range() const noexcept
{
  return range_;
}

void RuntimeTranscriptSelectionState::publish(ComposerSnapshot& snapshot) const
{
  if (!range_)
  {
    snapshot.transcript_selection_anchor_item = std::string::npos;
    snapshot.transcript_selection_anchor_line = 0;
    snapshot.transcript_selection_anchor_column = 0;
    snapshot.transcript_selection_focus_item = std::string::npos;
    snapshot.transcript_selection_focus_line = 0;
    snapshot.transcript_selection_focus_column = 0;
    return;
  }
  snapshot.transcript_selection_anchor_item = range_->anchor.item_index;
  snapshot.transcript_selection_anchor_line = range_->anchor.line_offset;
  snapshot.transcript_selection_anchor_column = range_->anchor.display_column;
  snapshot.transcript_selection_focus_item = range_->focus.item_index;
  snapshot.transcript_selection_focus_line = range_->focus.line_offset;
  snapshot.transcript_selection_focus_column = range_->focus.display_column;
}

std::optional<RuntimeTranscriptSelectionState::ItemSourceAuthority> RuntimeTranscriptSelectionState::source_authority(ComposerSnapshot const& snapshot,
                                                                                                                      std::size_t item_index)
{
  if (item_index >= snapshot.transcript.size())
    return std::nullopt;
  auto const& item = snapshot.transcript[item_index];
  ItemSourceAuthority source{.label = item.label,
                             .text = item.text,
                             .meta = item.meta,
                             .thinking = item.thinking,
                             .stream_id = item.stream_id,
                             .append_only_stream = item.append_only_stream,
                             .tool = item.tool.has_value()};
  if (item.tool)
  {
    source.tool_name = item.tool->name;
    source.tool_call_id = item.tool->call_id;
    source.tool_request_id = item.tool->request_id;
    source.tool_correlation_id = item.tool->correlation_id;
    source.tool_arguments = item.tool->arguments_json;
  }
  return source;
}

bool RuntimeTranscriptSelectionState::source_authority_compatible(ItemSourceAuthority const& previous, ItemSourceAuthority const& current)
{
  if (previous.tool != current.tool)
    return false;
  if (previous.tool)
  {
    auto const stable_ids_match = previous.tool_call_id == current.tool_call_id && previous.tool_request_id == current.tool_request_id &&
                                  previous.tool_correlation_id == current.tool_correlation_id;
    auto const has_stable_id = !previous.tool_call_id.empty() || !previous.tool_request_id.empty() || !previous.tool_correlation_id.empty();
    return previous.tool_name == current.tool_name && stable_ids_match && (has_stable_id || previous.tool_arguments == current.tool_arguments);
  }
  if (previous.label == current.label && previous.text == current.text && previous.meta == current.meta && previous.thinking == current.thinking)
    return true;
  return !previous.stream_id.empty() && previous.stream_id == current.stream_id && current.append_only_stream && current.text.starts_with(previous.text) &&
         current.thinking.starts_with(previous.thinking);
}

bool RuntimeTranscriptSelectionState::refresh_source_authorities_or_clear(ComposerSnapshot const& snapshot)
{
  if (!range_)
    return true;
  auto anchor = source_authority(snapshot, range_->anchor.item_index);
  auto focus = source_authority(snapshot, range_->focus.item_index);
  if (!anchor || !focus || (anchor_source_authority_ && !source_authority_compatible(*anchor_source_authority_, *anchor)) ||
      (focus_source_authority_ && !source_authority_compatible(*focus_source_authority_, *focus)))
  {
    clear();
    return false;
  }
  anchor_source_authority_ = std::move(anchor);
  focus_source_authority_ = std::move(focus);
  return true;
}

void RuntimeTranscriptSelectionState::rebind_authority(detail::TranscriptLayout const& layout, std::size_t layout_generation, std::size_t width,
                                                       ToolPresentation tool_presentation, bool thinking_visible, bool compact_spacing,
                                                       ComposerSnapshot const* snapshot)
{
  authority_generation_ = layout_generation;
  authority_width_ = width;
  authority_tool_presentation_ = tool_presentation;
  authority_thinking_visible_ = thinking_visible;
  authority_compact_spacing_ = compact_spacing;
  authority_valid_ = true;
  if (snapshot && !refresh_source_authorities_or_clear(*snapshot))
    return;
  remap_or_clear(layout);
  if (armed_press_endpoint_)
  {
    auto remapped = remap_endpoint(layout, *armed_press_endpoint_);
    if (remapped)
    {
      armed_press_endpoint_ = *remapped;
    }
    else
    {
      drag_ = DragKind::None;
      armed_header_item_.reset();
      armed_press_endpoint_.reset();
      armed_source_authority_.reset();
      armed_tool_header_ = false;
      armed_thinking_header_ = false;
    }
  }
}

void RuntimeTranscriptSelectionState::apply_item_index_shift(std::ptrdiff_t item_index_shift, detail::TranscriptLayout const& layout)
{
  if (item_index_shift == 0)
  {
    if (range_)
      remap_or_clear(layout);
    return;
  }
  if (armed_header_item_ && armed_press_endpoint_)
  {
    auto const shifted_header = shift_transcript_selection_item_index(*armed_header_item_, item_index_shift);
    auto const shifted_press = shift_transcript_selection_item_index(armed_press_endpoint_->item_index, item_index_shift);
    if (!shifted_header || !shifted_press)
    {
      drag_ = DragKind::None;
      armed_header_item_.reset();
      armed_press_endpoint_.reset();
      armed_source_authority_.reset();
      armed_tool_header_ = false;
      armed_thinking_header_ = false;
    }
    else
    {
      armed_header_item_ = *shifted_header;
      armed_press_endpoint_->item_index = *shifted_press;
      auto remapped_press = remap_endpoint(layout, *armed_press_endpoint_);
      if (remapped_press)
      {
        armed_press_endpoint_ = *remapped_press;
      }
      else
      {
        drag_ = DragKind::None;
        armed_header_item_.reset();
        armed_press_endpoint_.reset();
        armed_source_authority_.reset();
        armed_tool_header_ = false;
        armed_thinking_header_ = false;
      }
    }
  }
  if (!range_)
    return;
  auto anchor_item = shift_transcript_selection_item_index(range_->anchor.item_index, item_index_shift);
  auto focus_item = shift_transcript_selection_item_index(range_->focus.item_index, item_index_shift);
  if (!anchor_item || !focus_item)
  {
    clear();
    return;
  }
  range_->anchor.item_index = *anchor_item;
  range_->focus.item_index = *focus_item;
  remap_or_clear(layout);
}

void RuntimeTranscriptSelectionState::remap_or_clear(detail::TranscriptLayout const& layout)
{
  if (!range_)
    return;
  auto anchor = remap_endpoint(layout, range_->anchor);
  auto focus = remap_endpoint(layout, range_->focus);
  if (!anchor || !focus)
  {
    clear();
    return;
  }
  range_ = TranscriptSelectionRange{.anchor = *anchor, .focus = *focus};
}

bool RuntimeTranscriptSelectionState::has_compatible_authority(detail::TranscriptLayoutCache const& cache) const noexcept
{
  return authority_valid_ && cache.valid && cache.transcript_generation == authority_generation_ && cache.width == authority_width_ &&
         cache.tool_presentation == authority_tool_presentation_ && cache.thinking_visible == authority_thinking_visible_ &&
         cache.compact_spacing == authority_compact_spacing_;
}

bool RuntimeTranscriptSelectionState::ensure_authority(detail::TranscriptLayoutCache const& layout_cache, ComposerSnapshot const* snapshot)
{
  if (!layout_cache.valid)
  {
    authority_valid_ = false;
    clear();
    return false;
  }
  if (!has_compatible_authority(layout_cache))
  {
    rebind_authority(layout_cache.layout, layout_cache.transcript_generation, layout_cache.width, layout_cache.tool_presentation, layout_cache.thinking_visible,
                     layout_cache.compact_spacing, snapshot);
  }
  else
  {
    remap_or_clear(layout_cache.layout);
  }
  return authority_valid_;
}

std::optional<TranscriptSelectionViewport> RuntimeTranscriptSelectionState::viewport_for(ComposerSnapshot const& snapshot,
                                                                                         detail::TranscriptLayoutCache const& cache) const
{
  if (!cache.valid)
    return std::nullopt;
  auto const body = detail::transcript_body_screen_geometry(snapshot);
  if (!body.valid)
    return std::nullopt;
  auto const max_scroll = detail::cached_transcript_max_scroll_offset(cache, body.transcript_height);
  auto const scroll = std::min(snapshot.transcript_scroll_offset, max_scroll);
  auto const visible_start =
      cache.layout.lines.size() > body.transcript_height ? (cache.layout.lines.size() - body.transcript_height - scroll) : std::size_t{0};
  return TranscriptSelectionViewport{.overview_height = body.overview_height,
                                     .transcript_height = body.transcript_height,
                                     .content_width = body.content_width,
                                     .canvas_left = body.canvas_left,
                                     .max_scroll_offset = max_scroll,
                                     .scroll_offset = scroll,
                                     .visible_start = visible_start};
}

std::optional<TranscriptSelectionHit> RuntimeTranscriptSelectionState::hit_test(ComposerSnapshot const& snapshot, detail::TranscriptLayoutCache const& cache,
                                                                                std::size_t row, std::size_t column,
                                                                                std::ptrdiff_t frozen_to_live_item_index_shift) const
{
  auto const viewport = viewport_for(snapshot, cache);
  if (!viewport || row == 0 || column == 0)
    return std::nullopt;
  if (column <= viewport->canvas_left || column > viewport->canvas_left + viewport->content_width)
    return std::nullopt;
  auto const content_column = column - viewport->canvas_left;
  auto const row_index = row - 1;
  if (row_index < viewport->overview_height)
    return std::nullopt;
  auto const transcript_row = row_index - viewport->overview_height;
  if (transcript_row >= viewport->transcript_height)
    return std::nullopt;
  auto const absolute = viewport->visible_start + transcript_row;
  if (absolute >= cache.layout.lines.size())
    return std::nullopt;

  auto const plain = transcript_selection_plain_row(cache.layout.lines[absolute]);
  // Prefer end snap when the pointer is past mid-glyph so drag ranges feel natural.
  auto const display_column = snap_display_column(plain, content_column > 0 ? content_column - 1 : 0, /*prefer_end_on_half=*/true);
  auto endpoint = endpoint_for_absolute_line(cache.layout, absolute, display_column);
  if (!endpoint)
    return std::nullopt;

  TranscriptSelectionHit hit{.endpoint = *endpoint, .absolute_line = absolute};
  auto const position = position_for_item(cache.layout, endpoint->item_index);
  if (position && *position < cache.layout.content_starts.size() && cache.layout.content_starts[*position] == absolute)
  {
    hit.on_header = true;
    // Header kind is classified against the live transcript item after mapping any
    // deferred frozen→live cap shift. Geometry stays on the frozen layout authority.
    auto const live_item = shift_transcript_selection_item_index(endpoint->item_index, frozen_to_live_item_index_shift);
    if (live_item && *live_item < snapshot.transcript.size())
    {
      auto const& item = snapshot.transcript[*live_item];
      hit.tool_header = item.tool.has_value();
      hit.thinking_header = detail::transcript_item_has_boundable_thinking(item, viewport->content_width, snapshot.thinking_visible);
    }
  }
  return hit;
}

void RuntimeTranscriptSelectionState::begin_selection(TranscriptSelectionEndpoint const& endpoint, ComposerSnapshot const& snapshot,
                                                      RuntimeDraftState* draft_state, std::ptrdiff_t frozen_to_live_item_index_shift)
{
  if (draft_state)
    draft_state->clear_selection();
  range_ = TranscriptSelectionRange{.anchor = endpoint, .focus = endpoint};
  // Endpoints stay on the frozen layout authority; source identity is captured from the
  // live transcript item after mapping any deferred frozen→live cap shift.
  auto const live_item = shift_transcript_selection_item_index(endpoint.item_index, frozen_to_live_item_index_shift).value_or(endpoint.item_index);
  anchor_source_authority_ = source_authority(snapshot, live_item);
  focus_source_authority_ = anchor_source_authority_;
  drag_ = DragKind::Selecting;
  armed_header_item_.reset();
  armed_press_endpoint_.reset();
  armed_source_authority_.reset();
  armed_tool_header_ = false;
  armed_thinking_header_ = false;
}

void RuntimeTranscriptSelectionState::extend_selection(TranscriptSelectionEndpoint const& endpoint, ComposerSnapshot const& snapshot,
                                                       std::ptrdiff_t frozen_to_live_item_index_shift)
{
  auto const live_item = shift_transcript_selection_item_index(endpoint.item_index, frozen_to_live_item_index_shift).value_or(endpoint.item_index);
  if (!range_)
  {
    range_ = TranscriptSelectionRange{.anchor = endpoint, .focus = endpoint};
    anchor_source_authority_ = source_authority(snapshot, live_item);
  }
  else
  {
    range_->focus = endpoint;
  }
  focus_source_authority_ = source_authority(snapshot, live_item);
  drag_ = DragKind::Selecting;
  armed_header_item_.reset();
  armed_press_endpoint_.reset();
  armed_source_authority_.reset();
  armed_tool_header_ = false;
  armed_thinking_header_ = false;
}

void RuntimeTranscriptSelectionState::arm_header(TranscriptSelectionEndpoint endpoint, bool tool_header, bool thinking_header, ComposerSnapshot const& snapshot,
                                                 std::ptrdiff_t frozen_to_live_item_index_shift)
{
  drag_ = DragKind::HeaderArmed;
  armed_header_item_ = endpoint.item_index;
  armed_press_endpoint_ = endpoint;
  armed_tool_header_ = tool_header;
  armed_thinking_header_ = thinking_header;
  armed_source_authority_.reset();
  if (auto const live_item = shift_transcript_selection_item_index(endpoint.item_index, frozen_to_live_item_index_shift))
    armed_source_authority_ = source_authority(snapshot, *live_item);
}

bool RuntimeTranscriptSelectionState::toggle_header_at_frozen_item(ComposerSnapshot const& snapshot, std::size_t frozen_item_index, bool tool_header,
                                                                   bool thinking_header, std::ptrdiff_t frozen_to_live_item_index_shift,
                                                                   std::function<bool(std::size_t)> const& toggle_tool,
                                                                   std::function<bool(std::size_t)> const& toggle_thinking) const
{
  auto const live_item = shift_transcript_selection_item_index(frozen_item_index, frozen_to_live_item_index_shift);
  if (!live_item)
    return false;
  auto const current = source_authority(snapshot, *live_item);
  if (!current)
    return false;
  if (tool_header && toggle_tool)
  {
    if (!current->tool)
      return false;
    return toggle_tool(*live_item);
  }
  if (thinking_header && toggle_thinking)
  {
    if (current->tool)
      return false;
    return toggle_thinking(*live_item);
  }
  return false;
}

bool RuntimeTranscriptSelectionState::finish_header_click(ComposerSnapshot const& snapshot, std::ptrdiff_t frozen_to_live_item_index_shift,
                                                          std::function<bool(std::size_t)> const& toggle_tool,
                                                          std::function<bool(std::size_t)> const& toggle_thinking)
{
  if (!armed_header_item_)
    return false;
  auto const frozen_item = *armed_header_item_;
  auto const tool = armed_tool_header_;
  auto const thinking = armed_thinking_header_;
  auto const expected_source = armed_source_authority_;
  armed_header_item_.reset();
  armed_press_endpoint_.reset();
  armed_source_authority_.reset();
  armed_tool_header_ = false;
  armed_thinking_header_ = false;
  drag_ = DragKind::None;

  auto const live_item = shift_transcript_selection_item_index(frozen_item, frozen_to_live_item_index_shift);
  if (!live_item)
    return false;
  auto const current = source_authority(snapshot, *live_item);
  if (!current || (expected_source && !source_authority_compatible(*expected_source, *current)))
    return false;  // fail closed on eviction/replacement/source mismatch
  if (tool && toggle_tool)
    return current->tool ? toggle_tool(*live_item) : false;
  if (thinking && toggle_thinking)
    return !current->tool ? toggle_thinking(*live_item) : false;
  return false;
}

bool RuntimeTranscriptSelectionState::autoscroll_for_row(ComposerSnapshot& snapshot, TranscriptSelectionViewport const& viewport, std::size_t screen_row,
                                                         std::size_t& transcript_scroll_offset, bool treat_overview_as_top_edge)
{
  if (viewport.transcript_height == 0 || screen_row == 0)
    return false;
  auto const row_index = screen_row - 1;
  std::size_t transcript_row = 0;
  if (row_index < viewport.overview_height)
  {
    // New presses still exclude overview chrome via hit_test. During an owned drag,
    // overview rows (and anything above the first transcript row) act as the upper edge.
    if (!treat_overview_as_top_edge)
      return false;
    transcript_row = 0;
  }
  else
  {
    transcript_row = row_index - viewport.overview_height;
  }
  bool changed = false;
  if (transcript_row == 0 && transcript_scroll_offset < viewport.max_scroll_offset)
  {
    ++transcript_scroll_offset;
    changed = true;
  }
  else if (transcript_row + 1 >= viewport.transcript_height && transcript_scroll_offset > 0)
  {
    --transcript_scroll_offset;
    changed = true;
  }
  if (changed)
    snapshot.transcript_scroll_offset = transcript_scroll_offset;
  return changed;
}

TranscriptSelectionMouseResult RuntimeTranscriptSelectionState::handle_mouse(InputEvent const& event, ComposerSnapshot& snapshot,
                                                                             detail::TranscriptLayoutCache const& layout_cache, RuntimeDraftState* draft_state,
                                                                             std::size_t& transcript_scroll_offset,
                                                                             std::ptrdiff_t frozen_to_live_item_index_shift,
                                                                             std::function<bool(std::size_t)> const& toggle_tool,
                                                                             std::function<bool(std::size_t)> const& toggle_thinking)
{
  auto const key = event.key;
  if (key == Key::MousePointerCancel)
  {
    if (!dragging())
      return TranscriptSelectionMouseResult::Ignored;
    cancel_pointer_interaction();
    return TranscriptSelectionMouseResult::HandledNeedsRender;
  }
  if (key != Key::MouseLeftPress && key != Key::MouseLeftDrag && key != Key::MouseLeftRelease && key != Key::MouseLeftClick)
    return TranscriptSelectionMouseResult::Ignored;

  // Prompts/questions/permissions/select lists/palettes own the pointer first.
  if (snapshot.permission_prompt || snapshot.question_prompt || snapshot.select_list || snapshot.sidebar_drawer_visible)
  {
    if (dragging())
      clear();
    return TranscriptSelectionMouseResult::Ignored;
  }

  if (!ensure_authority(layout_cache, &snapshot))
    return TranscriptSelectionMouseResult::Ignored;

  auto const hit = hit_test(snapshot, layout_cache, event.mouse_row, event.mouse_column, frozen_to_live_item_index_shift);

  if (key == Key::MouseLeftPress)
  {
    if (!hit)
    {
      // Press outside transcript body: clear armed header / selection drag, keep
      // existing selection until a new body selection starts or composer claims.
      if (drag_ == DragKind::HeaderArmed || drag_ == DragKind::Selecting)
        cancel_pointer_interaction();
      return TranscriptSelectionMouseResult::Ignored;
    }
    if (hit->on_header && (hit->tool_header || hit->thinking_header))
    {
      arm_header(hit->endpoint, hit->tool_header, hit->thinking_header, snapshot, frozen_to_live_item_index_shift);
      // Do not start a selection yet; drag converts the arm into selection.
      snapshot.status = "selection started";
      return TranscriptSelectionMouseResult::HandledNeedsRender;
    }
    begin_selection(hit->endpoint, snapshot, draft_state, frozen_to_live_item_index_shift);
    snapshot.status = "selection active";
    return TranscriptSelectionMouseResult::HandledNeedsRender;
  }

  if (key == Key::MouseLeftDrag)
  {
    if (drag_ == DragKind::None && !hit)
      return TranscriptSelectionMouseResult::Ignored;

    auto viewport = viewport_for(snapshot, layout_cache);
    if (viewport)
    {
      // During an owned drag (or extending an existing range), overview chrome is the top edge.
      auto const treat_overview_as_top = drag_ == DragKind::Selecting || drag_ == DragKind::HeaderArmed || range_.has_value();
      static_cast<void>(autoscroll_for_row(snapshot, *viewport, event.mouse_row, transcript_scroll_offset, treat_overview_as_top));
    }

    // Re-hit after possible autoscroll against the same frozen authority.
    auto drag_hit = hit_test(snapshot, layout_cache, event.mouse_row, event.mouse_column, frozen_to_live_item_index_shift);
    if (!drag_hit)
    {
      // Clamp to nearest transcript edge row when dragging past the viewport.
      viewport = viewport_for(snapshot, layout_cache);
      if (!viewport || layout_cache.layout.lines.empty())
        return dragging() ? TranscriptSelectionMouseResult::HandledNeedsRender : TranscriptSelectionMouseResult::Ignored;
      auto const edge_row =
          event.mouse_row <= viewport->overview_height + 1 ? viewport->overview_height + 1 : viewport->overview_height + viewport->transcript_height;
      auto const edge_column = std::clamp(event.mouse_column, viewport->canvas_left + 1, viewport->canvas_left + viewport->content_width);
      drag_hit = hit_test(snapshot, layout_cache, edge_row, edge_column, frozen_to_live_item_index_shift);
      if (!drag_hit)
        return dragging() ? TranscriptSelectionMouseResult::HandledNeedsRender : TranscriptSelectionMouseResult::Ignored;
    }

    if (drag_ == DragKind::HeaderArmed)
    {
      // Anchor at the original press, not at the first motion endpoint.
      auto const pressed = armed_press_endpoint_;
      if (!pressed)
      {
        cancel_pointer_interaction();
        return TranscriptSelectionMouseResult::Handled;
      }
      begin_selection(*pressed, snapshot, draft_state, frozen_to_live_item_index_shift);
      extend_selection(drag_hit->endpoint, snapshot, frozen_to_live_item_index_shift);
      snapshot.status = "selection active";
      return TranscriptSelectionMouseResult::HandledNeedsRender;
    }
    if (drag_ == DragKind::Selecting || range_)
    {
      if (drag_ != DragKind::Selecting && draft_state)
        draft_state->clear_selection();
      extend_selection(drag_hit->endpoint, snapshot, frozen_to_live_item_index_shift);
      snapshot.status = "selection active";
      return TranscriptSelectionMouseResult::HandledNeedsRender;
    }
    return TranscriptSelectionMouseResult::Ignored;
  }

  if (key == Key::MouseLeftRelease)
  {
    if (drag_ == DragKind::HeaderArmed)
    {
      auto const pressed = armed_press_endpoint_;
      if (!hit || !pressed)
      {
        cancel_pointer_interaction();
        return TranscriptSelectionMouseResult::Handled;
      }
      auto const moved = pressed->item_index != hit->endpoint.item_index || pressed->line_offset != hit->endpoint.line_offset ||
                         pressed->display_column != hit->endpoint.display_column;
      if (moved)
      {
        begin_selection(*pressed, snapshot, draft_state, frozen_to_live_item_index_shift);
        extend_selection(hit->endpoint, snapshot, frozen_to_live_item_index_shift);
        drag_ = DragKind::None;
        snapshot.status = "selection active";
        return TranscriptSelectionMouseResult::HandledNeedsRender;
      }
      // Map the frozen armed item through the exact accumulated deferred shift and
      // toggle only the same source item; fail closed on eviction/replacement.
      auto const toggled = finish_header_click(snapshot, frozen_to_live_item_index_shift, toggle_tool, toggle_thinking);
      return toggled ? TranscriptSelectionMouseResult::HandledNeedsRender : TranscriptSelectionMouseResult::Handled;
    }
    if (drag_ == DragKind::Selecting)
    {
      if (hit)
        extend_selection(hit->endpoint, snapshot, frozen_to_live_item_index_shift);
      drag_ = DragKind::None;
      if (range_ && !endpoint_less(range_->anchor, range_->focus, layout_cache.layout) && !endpoint_less(range_->focus, range_->anchor, layout_cache.layout) &&
          range_->anchor.display_column == range_->focus.display_column)
      {
        // Zero-width press/release: keep caret-less empty selection cleared.
        clear();
        snapshot.status.clear();
      }
      else if (range_)
      {
        snapshot.status = "selection active";
      }
      return TranscriptSelectionMouseResult::HandledNeedsRender;
    }
    return TranscriptSelectionMouseResult::Ignored;
  }

  // Complete click (ncurses CLICKED or terminals that only emit click).
  if (key == Key::MouseLeftClick)
  {
    if (!hit)
      return TranscriptSelectionMouseResult::Ignored;
    if (hit->on_header && (hit->tool_header || hit->thinking_header))
    {
      clear();
      auto const toggled = toggle_header_at_frozen_item(snapshot, hit->endpoint.item_index, hit->tool_header, hit->thinking_header,
                                                        frozen_to_live_item_index_shift, toggle_tool, toggle_thinking);
      return toggled ? TranscriptSelectionMouseResult::HandledNeedsRender : TranscriptSelectionMouseResult::Handled;
    }
    // Body click without drag: place a collapsed selection cleared immediately
    // so ordinary clicks do not leave a sticky highlight.
    clear();
    return TranscriptSelectionMouseResult::Ignored;
  }

  return TranscriptSelectionMouseResult::Ignored;
}

bool RuntimeTranscriptSelectionState::copy_selection(ComposerSnapshot& snapshot, detail::TranscriptLayoutCache const& layout_cache)
{
  if (!range_)
  {
    snapshot.status = "no selection to copy";
    static_cast<void>(beep());
    return false;
  }
  if (!has_compatible_authority(layout_cache))
  {
    snapshot.status = "selection unavailable in current view";
    static_cast<void>(beep());
    return false;
  }
  auto extracted = extract_transcript_selection_text(layout_cache.layout, *range_, kSelectionCopySoftCeiling);
  if (extracted.oversize)
  {
    snapshot.status = "selection too large to copy";
    static_cast<void>(beep());
    return false;
  }
  if (extracted.text.empty())
  {
    snapshot.status = "no selection to copy";
    static_cast<void>(beep());
    return false;
  }
  auto const copied = runtime_transcript::copy_text_to_terminal_clipboard(extracted.text);
  snapshot.status = copied ? "copied selection to clipboard" : "clipboard copy failed";
  if (!copied)
    static_cast<void>(beep());
  // Keep selection after success.
  return copied;
}

}  // namespace ava::tui
