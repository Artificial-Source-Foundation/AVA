#include "sys.h"
#include "ava/tui/composer.h"
#include "ava/tui/composer_internal.h"
#include "ava/tui/runtime_navigation_internal.h"
#include "ava/tui/runtime_render_internal.h"
#include "ava/tui/runtime_state_internal.h"
#include "ava/tui/runtime_transcript_search_internal.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace ava::tui {
namespace detail {
namespace {

std::string strip_terminal_sequences(std::string_view text)
{
  std::string stripped;
  stripped.reserve(text.size());
  for (std::size_t index = 0; index < text.size();)
  {
    auto const before = index;
    if (skip_sgr_sequence(text, index) || skip_osc_sequence(text, index))
      continue;
    stripped.push_back(text[before]);
    ++index;
  }
  return sanitize_terminal_text(stripped);
}

bool blank(std::string_view text)
{
  return std::ranges::all_of(text, [](char ch) { return ch == ' '; });
}

std::string trim_horizontal_space(std::string text)
{
  auto const first = text.find_first_not_of(' ');
  if (first == std::string::npos)
    return {};
  auto const last = text.find_last_not_of(' ');
  return text.substr(first, last - first + 1);
}

std::string bound_utf8_detail(std::string text)
{
  if (text.size() <= kMaxTranscriptSearchDetailBytes)
    return text;
  constexpr std::string_view suffix = "...";
  auto end = kMaxTranscriptSearchDetailBytes - suffix.size();
  while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0U) == 0x80U) --end;
  text.resize(end);
  text += suffix;
  return text;
}

std::string match_identity(TranscriptItem const& item)
{
  std::string identity;
  if (item.tool)
  {
    identity = "tool";
    if (!item.tool->name.empty())
      identity += " · " + item.tool->name;
  }
  else if (item.label == "you" || item.label == "user")
  {
    identity = "user";
  }
  else if (item.label == "ava" || item.label == "assistant")
  {
    identity = "assistant";
  }
  else if (item.label.empty())
  {
    identity = "transcript item";
  }
  else
  {
    identity = item.label;
  }
  identity = sanitize_terminal_text(identity);
  if (identity.size() > 80)
  {
    auto end = std::size_t{77};
    while (end > 0 && (static_cast<unsigned char>(identity[end]) & 0xC0U) == 0x80U) --end;
    identity.resize(end);
    identity += "...";
  }
  return identity;
}

char fold_ascii(char ch) noexcept
{
  auto const byte = static_cast<unsigned char>(ch);
  if (byte >= 'A' && byte <= 'Z')
    return static_cast<char>(byte + ('a' - 'A'));
  return ch;
}

}  // namespace

bool transcript_search_query_valid(std::string_view query) noexcept
{
  if (query.size() > kMaxTranscriptSearchQueryBytes)
    return false;
  return std::ranges::none_of(query, [](char ch) {
    auto const byte = static_cast<unsigned char>(ch);
    return byte < 0x20 || byte == 0x7F;
  });
}

bool transcript_search_literal_match(std::string_view candidate, std::string_view query) noexcept
{
  if (query.empty())
    return true;
  if (query.size() > candidate.size())
    return false;
  for (std::size_t start = 0; start + query.size() <= candidate.size(); ++start)
  {
    bool equal = true;
    for (std::size_t offset = 0; offset < query.size(); ++offset)
    {
      if (fold_ascii(candidate[start + offset]) != fold_ascii(query[offset]))
      {
        equal = false;
        break;
      }
    }
    if (equal)
      return true;
  }
  return false;
}

std::vector<TranscriptSearchMatch> build_transcript_search_matches(ComposerSnapshot const& snapshot, TranscriptLayout const& layout, std::string_view query)
{
  std::vector<TranscriptSearchMatch> matches;
  if (!transcript_search_query_valid(query) || layout.message_starts.size() != layout.message_item_indices.size())
    return matches;

  for (std::size_t position = 0; position < layout.message_starts.size(); ++position)
  {
    auto const item_index = layout.message_item_indices[position];
    if (item_index >= snapshot.transcript.size())
      continue;
    auto const start = std::min(layout.message_starts[position], layout.lines.size());
    auto const end = position + 1 < layout.message_starts.size() ? std::min(layout.message_starts[position + 1], layout.lines.size()) : layout.lines.size();
    auto matched = query.empty();
    std::string detail;
    std::string first_nonblank;
    for (auto line_index = start; line_index < end; ++line_index)
    {
      auto line = strip_terminal_sequences(layout.lines[line_index]);
      if (first_nonblank.empty() && !blank(line))
        first_nonblank = line;
      if (!matched && transcript_search_literal_match(line, query))
      {
        matched = true;
        detail = std::move(line);
      }
    }
    if (!matched)
      continue;
    if (detail.empty())
      detail = std::move(first_nonblank);
    detail = bound_utf8_detail(trim_horizontal_space(std::move(detail)));
    matches.push_back(
        TranscriptSearchMatch{.item_index = item_index, .identity = match_identity(snapshot.transcript[item_index]), .detail = std::move(detail)});
  }
  return matches;
}

}  // namespace detail

TranscriptSearchController::TranscriptSearchController(RuntimePresentationState& presentation_state, RuntimeRenderer& renderer,
                                                       RuntimeNavigationController& navigation, ActiveSelectList& active_select_list)
    : presentation_state_(presentation_state), renderer_(renderer), navigation_(navigation), active_select_list_(active_select_list)
{
}

bool TranscriptSearchController::is_open() const noexcept
{
  return active_select_list_ == ActiveSelectList::TranscriptSearch;
}

void TranscriptSearchController::refresh_authoritative_layout()
{
  auto& snapshot = presentation_state_.snapshot;
  renderer_.synchronize_detached_transcript_layout();
  auto const [width, height] = terminal_size();
  snapshot.width = width;
  snapshot.height = height;
  static_cast<void>(detail::composer_max_transcript_scroll_offset_cached(snapshot, width, height, renderer_.completion_cache,
                                                                         snapshot.file_references_generation, renderer_.transcript_layout_cache,
                                                                         snapshot.transcript_generation));
}

bool TranscriptSearchController::open(std::string query)
{
  auto& snapshot = presentation_state_.snapshot;
  if (!detail::transcript_search_query_valid(query))
  {
    snapshot.status = query.size() > detail::kMaxTranscriptSearchQueryBytes ? "search query is too long (maximum 1024 bytes)"
                                                                            : "search query must be a single printable line";
    return false;
  }

  refresh_authoritative_layout();
  auto const max_scroll = detail::composer_max_transcript_scroll_offset_cached(snapshot, snapshot.width, snapshot.height, renderer_.completion_cache,
                                                                               snapshot.file_references_generation, renderer_.transcript_layout_cache,
                                                                               snapshot.transcript_generation);
  saved_scroll_offset_ = std::min(renderer_.transcript_scroll_offset, max_scroll);
  saved_transcript_generation_ = snapshot.transcript_generation;
  saved_width_ = snapshot.width;
  saved_height_ = snapshot.height;
  saved_viewport_anchor_ = detail::capture_transcript_viewport_anchor(renderer_.transcript_layout_cache.layout, max_scroll, saved_scroll_offset_);
  accumulated_item_index_shift_ = 0;
  query_ = std::move(query);
  active_select_list_ = ActiveSelectList::TranscriptSearch;
  rebuild(std::nullopt, std::numeric_limits<std::size_t>::max());
  snapshot.status = matches_.empty() ? "no transcript matches" : "transcript search opened";
  return true;
}

void TranscriptSearchController::rebuild(std::optional<std::size_t> selected_item_index, std::size_t fallback_selection)
{
  auto& snapshot = presentation_state_.snapshot;
  matches_ = detail::build_transcript_search_matches(snapshot, renderer_.transcript_layout_cache.layout, query_);
  SelectListView view{
      .title = "Transcript search",
      .subtitle = std::to_string(matches_.size()) + (matches_.size() == 1 ? " item" : " items"),
      .items = {},
      .selected_item_index = 0,
      .query = query_,
      .placeholder = "Literal query (empty lists all)",
      .empty_text = "No transcript matches",
      .footer_hint = "Enter jump · Esc restore",
  };
  view.items.reserve(matches_.size());
  for (auto const& match : matches_)
  {
    // The controller performs literal filtering. An exact duplicate query in
    // every value keeps the select-list chrome's generic filter stable and
    // preserves chronological order without exposing an alternate search path.
    view.items.push_back(SelectListItemView{.value = query_,
                                            .label = match.identity,
                                            .description = {},
                                            .group = {},
                                            .detail = match.detail,
                                            .badge = {},
                                            .current = false,
                                            .enabled = true,
                                            .disabled_reason = {}});
  }
  if (!matches_.empty())
  {
    auto selected = matches_.end();
    if (selected_item_index)
      selected = std::ranges::find_if(matches_, [&](auto const& match) { return match.item_index == *selected_item_index; });
    if (selected != matches_.end())
      view.selected_item_index = static_cast<std::size_t>(selected - matches_.begin());
    else if (fallback_selection == std::numeric_limits<std::size_t>::max())
      view.selected_item_index = matches_.size() - 1;
    else
      view.selected_item_index = std::min(fallback_selection, matches_.size() - 1);
  }
  snapshot.select_list = std::move(view);
}

void TranscriptSearchController::refresh(std::ptrdiff_t item_index_shift)
{
  if (!is_open())
    return;
  std::optional<std::size_t> selected_item_index;
  auto fallback_selection = std::size_t{0};
  if (presentation_state_.snapshot.select_list)
  {
    fallback_selection = presentation_state_.snapshot.select_list->selected_item_index;
    if (fallback_selection < matches_.size())
    {
      auto const shifted = static_cast<std::ptrdiff_t>(matches_[fallback_selection].item_index) + item_index_shift;
      if (shifted >= 0)
        selected_item_index = static_cast<std::size_t>(shifted);
    }
  }
  accumulated_item_index_shift_ += item_index_shift;
  refresh_authoritative_layout();
  rebuild(selected_item_index, fallback_selection);
}

void TranscriptSearchController::restore_saved_viewport(std::string status)
{
  auto& snapshot = presentation_state_.snapshot;
  snapshot.select_list.reset();
  active_select_list_ = ActiveSelectList::None;
  refresh_authoritative_layout();
  auto const max_scroll = detail::composer_max_transcript_scroll_offset_cached(snapshot, snapshot.width, snapshot.height, renderer_.completion_cache,
                                                                               snapshot.file_references_generation, renderer_.transcript_layout_cache,
                                                                               snapshot.transcript_generation);
  auto const exact_restore =
      snapshot.width == saved_width_ && snapshot.height == saved_height_ &&
      (saved_scroll_offset_ == 0 || (snapshot.transcript_generation == saved_transcript_generation_ && accumulated_item_index_shift_ == 0));
  renderer_.transcript_scroll_offset = exact_restore ? std::min(saved_scroll_offset_, max_scroll)
                                       : saved_viewport_anchor_.valid
                                           ? detail::restore_transcript_viewport_anchor(saved_viewport_anchor_, renderer_.transcript_layout_cache.layout,
                                                                                        max_scroll, accumulated_item_index_shift_)
                                           : std::min(saved_scroll_offset_, max_scroll);
  renderer_.discard_deferred_detached_transcript_update();
  if (renderer_.transcript_scroll_offset > 0)
  {
    if (!renderer_.detached_sidebar_snapshot)
      renderer_.detached_sidebar_snapshot = presentation_state_.sidebar;
  }
  else
  {
    renderer_.detached_new_output_count = 0;
    renderer_.detached_sidebar_snapshot.reset();
  }
  snapshot.status = std::move(status);
  matches_.clear();
  query_.clear();
}

std::optional<bool> TranscriptSearchController::handle_input(InputEvent const& event)
{
  if (!is_open())
    return std::nullopt;
  auto& snapshot = presentation_state_.snapshot;
  if (!snapshot.select_list)
  {
    active_select_list_ = ActiveSelectList::None;
    return renderer_.request_render();
  }

  SelectListInputResult input_result;
  if (event.key == Key::MouseLeftClick)
  {
    auto const clicked = select_list_selection_for_screen_position(snapshot, event.mouse_row, event.mouse_column);
    if (!clicked)
      return renderer_.request_render();
    input_result = SelectListInputResult{.selected_item_index = *clicked, .query = query_, .action = SelectListInputAction::Resolve};
  }
  else
  {
    input_result = handle_select_list_input(*snapshot.select_list, event);
  }

  if (input_result.action == SelectListInputAction::Cancel)
  {
    restore_saved_viewport("transcript search canceled");
    return renderer_.request_render();
  }
  if (input_result.action == SelectListInputAction::Resolve)
  {
    if (input_result.selected_item_index < matches_.size())
    {
      auto const item_index = matches_[input_result.selected_item_index].item_index;
      snapshot.select_list.reset();
      active_select_list_ = ActiveSelectList::None;
      matches_.clear();
      query_.clear();
      navigation_.jump_to_transcript_item(item_index, "transcript match");
    }
    return renderer_.request_render();
  }
  if (input_result.action == SelectListInputAction::Redraw)
  {
    std::optional<std::size_t> selected_item_index;
    if (input_result.query == query_)
    {
      if (input_result.selected_item_index < matches_.size())
        selected_item_index = matches_[input_result.selected_item_index].item_index;
    }
    else if (snapshot.select_list->selected_item_index < matches_.size())
    {
      selected_item_index = matches_[snapshot.select_list->selected_item_index].item_index;
    }
    if (detail::transcript_search_query_valid(input_result.query))
    {
      query_ = std::move(input_result.query);
    }
    else
    {
      snapshot.status = "search query is limited to 1024 printable bytes";
    }
    refresh_authoritative_layout();
    rebuild(selected_item_index, input_result.selected_item_index);
    return renderer_.request_render();
  }
  return true;
}

void TranscriptSearchController::close_before_prompt()
{
  if (is_open())
    restore_saved_viewport({});
}

}  // namespace ava::tui
