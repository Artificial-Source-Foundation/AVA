#include "sys.h"
#include "ava/tui/composer.h"
#include "ava/tui/composer_internal.h"
#include "ava/tui/runtime_navigation_internal.h"
#include "ava/tui/runtime_render_internal.h"
#include "ava/tui/runtime_state_internal.h"
#include "ava/tui/runtime_transcript_search_internal.h"

#include <algorithm>
#include <array>
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

bool context_gathering_item(TranscriptItem const& item)
{
  if (!item.tool)
    return false;
  auto name = item.tool->name;
  std::ranges::transform(name, name.begin(), fold_ascii);
  return name == "read_file" || name == "glob" || name == "grep" || name == "list_directory" || name == "lsp_diagnostics";
}

class FoldedLiteralMatcher final
{
 public:
  explicit FoldedLiteralMatcher(std::string_view query) noexcept : query_(query)
  {
    if (query_.size() > kMaxTranscriptSearchQueryBytes)
      return;
    for (std::size_t index = 1, matched = 0; index < query_.size(); ++index)
    {
      while (matched > 0 && fold_ascii(query_[index]) != fold_ascii(query_[matched])) matched = prefix_lengths_[matched - 1];
      if (fold_ascii(query_[index]) == fold_ascii(query_[matched]))
        ++matched;
      prefix_lengths_[index] = matched;
    }
  }

  [[nodiscard]] std::optional<std::size_t> match_offset(std::string_view candidate) const noexcept
  {
    if (query_.empty())
      return std::size_t{0};
    if (query_.size() > candidate.size() || query_.size() > kMaxTranscriptSearchQueryBytes)
      return std::nullopt;
    for (std::size_t index = 0, matched = 0; index < candidate.size(); ++index)
    {
      while (matched > 0 && fold_ascii(candidate[index]) != fold_ascii(query_[matched])) matched = prefix_lengths_[matched - 1];
      if (fold_ascii(candidate[index]) == fold_ascii(query_[matched]))
        ++matched;
      if (matched == query_.size())
        return index + 1 - query_.size();
    }
    return std::nullopt;
  }

  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT

 private:
  std::string_view query_;
  std::array<std::size_t, kMaxTranscriptSearchQueryBytes> prefix_lengths_{};
};

std::optional<std::size_t> projection_match_offset(TranscriptSearchProjection const& projection, FoldedLiteralMatcher const& matcher) noexcept
{
  // Search both legal presentations of rendered row boundaries with one KMP
  // prefix table: one space preserves wrapped phrases, while omission rejoins
  // hard-wrapped tokens. Both passes remain linear in projected bytes.
  if (auto const offset = matcher.match_offset(projection.searchable_text))
    return offset;
  auto const unspaced_offset = matcher.match_offset(projection.unspaced_searchable_text);
  if (!unspaced_offset)
    return std::nullopt;
  auto const boundaries_before_match = std::ranges::upper_bound(projection.row_boundary_offsets, *unspaced_offset);
  return *unspaced_offset + static_cast<std::size_t>(boundaries_before_match - projection.row_boundary_offsets.begin());
}

TranscriptSearchProjection build_projection(ComposerSnapshot const& snapshot, TranscriptLayout const& layout, std::size_t position)
{
  auto const item_index = layout.message_item_indices[position];
  auto const start = std::min(layout.message_starts[position], layout.lines.size());
  auto const end = position + 1 < layout.message_starts.size() ? std::min(layout.message_starts[position + 1], layout.lines.size()) : layout.lines.size();
  TranscriptSearchProjection projection{.available = true,
                                        .context_gathering = context_gathering_item(snapshot.transcript[item_index]),
                                        .identity = match_identity(snapshot.transcript[item_index]),
                                        .searchable_text = {},
                                        .unspaced_searchable_text = {},
                                        .row_boundary_offsets = {},
                                        .default_detail = {}};
  bool first_row = true;
  for (auto line_index = start; line_index < end; ++line_index)
  {
    auto line = trim_horizontal_space(strip_terminal_sequences(layout.lines[line_index]));
    if (snapshot.transcript[item_index].tool && line.starts_with("│"))
      line = trim_horizontal_space(line.substr(std::string_view("│").size()));
    if (projection.default_detail.empty() && !blank(line))
      projection.default_detail = line;
    if (!first_row)
    {
      projection.row_boundary_offsets.push_back(projection.unspaced_searchable_text.size());
      projection.searchable_text.push_back(' ');
    }
    projection.searchable_text += line;
    projection.unspaced_searchable_text += line;
    first_row = false;
  }
  projection.default_detail = bound_utf8_detail(std::move(projection.default_detail));
  return projection;
}

std::string relevant_detail(TranscriptSearchProjection const& projection, std::string_view query, std::size_t match_offset)
{
  if (query.empty())
    return projection.default_detail;
  constexpr auto kContextBytes = std::size_t{72};
  auto start = match_offset > kContextBytes ? match_offset - kContextBytes : std::size_t{0};
  while (start > 0 && (static_cast<unsigned char>(projection.searchable_text[start]) & 0xC0U) == 0x80U) --start;
  auto detail = projection.searchable_text.substr(start);
  if (start > 0)
    detail = "... " + detail;
  return bound_utf8_detail(trim_horizontal_space(std::move(detail)));
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
  return FoldedLiteralMatcher(query).match_offset(candidate).has_value();
}

std::optional<std::size_t> shift_transcript_search_item_index(std::optional<std::size_t> item_index, std::ptrdiff_t item_index_shift) noexcept
{
  if (!item_index)
    return std::nullopt;
  auto const shifted = static_cast<std::ptrdiff_t>(*item_index) + item_index_shift;
  return shifted >= 0 ? std::optional<std::size_t>{static_cast<std::size_t>(shifted)} : std::nullopt;
}

void TranscriptSearchProjectionCache::rebuild_range(ComposerSnapshot const& snapshot, TranscriptLayout const& layout, std::size_t first_item_index,
                                                    std::size_t past_last_item_index)
{
  if (layout.message_starts.size() != layout.message_item_indices.size())
    return;
  for (std::size_t position = 0; position < layout.message_starts.size(); ++position)
  {
    auto const item_index = layout.message_item_indices[position];
    if (item_index < first_item_index || item_index >= past_last_item_index || item_index >= snapshot.transcript.size())
      continue;
    projections_[item_index] = build_projection(snapshot, layout, position);
    ++projection_build_count_;
  }
}

TranscriptSearchUpdate TranscriptSearchProjectionCache::rebuild_all(ComposerSnapshot const& snapshot, TranscriptLayout const& layout)
{
  auto previous_matches = matches_;
  projections_.assign(snapshot.transcript.size(), TranscriptSearchProjection{});
  rebuild_range(snapshot, layout, 0, projections_.size());
  return replace_matches(std::move(previous_matches));
}

void TranscriptSearchProjectionCache::clear()
{
  projections_.clear();
  query_.clear();
  matches_.clear();
}

TranscriptSearchUpdate TranscriptSearchProjectionCache::update_query(std::string query)
{
  if (!transcript_search_query_valid(query) || query == query_)
    return TranscriptSearchUpdate{.first_changed_match_row = matches_.size()};
  auto previous_matches = matches_;
  query_ = std::move(query);
  auto update = replace_matches(std::move(previous_matches));
  update.first_changed_match_row = 0;
  return update;
}

void TranscriptSearchProjectionCache::rebuild_affected(ComposerSnapshot const& snapshot, TranscriptLayout const& layout, std::vector<bool> const& affected)
{
  auto first = std::size_t{0};
  while (first < affected.size())
  {
    first = static_cast<std::size_t>(std::find(affected.begin() + static_cast<std::ptrdiff_t>(first), affected.end(), true) - affected.begin());
    if (first == affected.size())
      return;
    auto past_last = first + 1;
    while (past_last < affected.size() && affected[past_last]) ++past_last;
    rebuild_range(snapshot, layout, first, past_last);
    first = past_last;
  }
}

TranscriptSearchUpdate TranscriptSearchProjectionCache::replace_matches(std::vector<TranscriptSearchMatch> previous_matches, std::vector<bool> const* affected)
{
  FoldedLiteralMatcher matcher(query_);
  if (affected)
  {
    std::erase_if(matches_, [&](TranscriptSearchMatch const& match) { return match.item_index >= affected->size() || (*affected)[match.item_index]; });
  }
  else
  {
    matches_.clear();
    matches_.reserve(projections_.size());
  }

  for (std::size_t item_index = 0; item_index < projections_.size(); ++item_index)
  {
    if (affected && !(*affected)[item_index])
      continue;
    ++match_scan_count_;
    auto const& projection = projections_[item_index];
    if (!projection.available)
      continue;
    auto const offset = projection_match_offset(projection, matcher);
    if (!offset)
      continue;
    auto match = TranscriptSearchMatch{.item_index = item_index, .identity = projection.identity, .detail = relevant_detail(projection, query_, *offset)};
    if (!affected || matches_.empty() || matches_.back().item_index < item_index)
    {
      matches_.push_back(std::move(match));
    }
    else
    {
      auto const insertion = std::ranges::lower_bound(matches_, item_index, {}, &TranscriptSearchMatch::item_index);
      matches_.insert(insertion, std::move(match));
    }
  }

  auto first_changed = std::size_t{0};
  while (first_changed < previous_matches.size() && first_changed < matches_.size() &&
         previous_matches[first_changed].identity == matches_[first_changed].identity &&
         previous_matches[first_changed].detail == matches_[first_changed].detail)
    ++first_changed;
  return TranscriptSearchUpdate{.first_changed_match_row = first_changed};
}

TranscriptSearchUpdate TranscriptSearchProjectionCache::refresh_after_transcript_mutation(ComposerSnapshot const& snapshot, TranscriptLayout const& layout,
                                                                                          std::ptrdiff_t item_index_shift, std::size_t changed_from_item_index)
{
  auto previous_matches = matches_;
  if (item_index_shift < 0)
  {
    auto const evicted = std::min(static_cast<std::size_t>(-item_index_shift), projections_.size());
    projections_.erase(projections_.begin(), projections_.begin() + static_cast<std::ptrdiff_t>(evicted));
  }
  else if (item_index_shift > 0)
  {
    projections_.insert(projections_.begin(), static_cast<std::size_t>(item_index_shift), TranscriptSearchProjection{});
  }
  projections_.resize(snapshot.transcript.size());

  std::vector<TranscriptSearchMatch> realigned_matches;
  realigned_matches.reserve(matches_.size());
  for (auto& match : matches_)
  {
    auto shifted = shift_transcript_search_item_index(match.item_index, item_index_shift);
    if (!shifted || *shifted >= projections_.size())
      continue;
    match.item_index = *shifted;
    realigned_matches.push_back(std::move(match));
  }
  matches_ = std::move(realigned_matches);

  std::vector<bool> affected(projections_.size(), false);
  changed_from_item_index = std::min(changed_from_item_index, projections_.size());
  std::fill(affected.begin() + static_cast<std::ptrdiff_t>(changed_from_item_index), affected.end(), true);
  auto mark_context_run_start = [&](std::size_t item_index, auto const& is_context_item) {
    while (item_index > 0 && is_context_item(item_index - 1)) --item_index;
    affected[item_index] = true;
  };
  if (changed_from_item_index > 0 && changed_from_item_index < projections_.size() && projections_[changed_from_item_index - 1].context_gathering &&
      projections_[changed_from_item_index].context_gathering)
  {
    mark_context_run_start(changed_from_item_index - 1, [&](std::size_t item_index) { return projections_[item_index].context_gathering; });
  }
  if (changed_from_item_index > 0 && changed_from_item_index < snapshot.transcript.size() &&
      context_gathering_item(snapshot.transcript[changed_from_item_index - 1]) && context_gathering_item(snapshot.transcript[changed_from_item_index]))
  {
    mark_context_run_start(changed_from_item_index - 1, [&](std::size_t item_index) { return context_gathering_item(snapshot.transcript[item_index]); });
  }
  if (item_index_shift < 0 && !affected.empty())
    affected[0] = true;
  else if (item_index_shift > 0)
  {
    auto const join_end = std::min(projections_.size(), static_cast<std::size_t>(item_index_shift) + 1);
    std::fill(affected.begin(), affected.begin() + static_cast<std::ptrdiff_t>(join_end), true);
  }
  for (std::size_t item_index = 0; item_index < affected.size(); ++item_index)
  {
    if (affected[item_index])
      projections_[item_index] = TranscriptSearchProjection{};
  }
  rebuild_affected(snapshot, layout, affected);
  return replace_matches(std::move(previous_matches), &affected);
}

std::vector<TranscriptSearchMatch> const& TranscriptSearchProjectionCache::matches() const noexcept
{
  return matches_;
}

std::string const& TranscriptSearchProjectionCache::query() const noexcept
{
  return query_;
}

std::size_t TranscriptSearchProjectionCache::projection_build_count() const noexcept
{
  return projection_build_count_;
}

std::size_t TranscriptSearchProjectionCache::match_scan_count() const noexcept
{
  return match_scan_count_;
}

std::vector<TranscriptSearchMatch> build_transcript_search_matches(ComposerSnapshot const& snapshot, TranscriptLayout const& layout, std::string_view query)
{
  if (!transcript_search_query_valid(query))
    return {};
  TranscriptSearchProjectionCache cache;
  static_cast<void>(cache.update_query(std::string(query)));
  static_cast<void>(cache.rebuild_all(snapshot, layout));
  return cache.matches();
}

void update_transcript_search_select_list_rows(SelectListView& view, std::vector<TranscriptSearchMatch> const& matches, std::string_view query,
                                               std::size_t first_changed_match_row, std::size_t& modal_row_build_count)
{
  first_changed_match_row = std::min(first_changed_match_row, matches.size());
  first_changed_match_row = std::min(first_changed_match_row, view.items.size());
  view.items.resize(first_changed_match_row);
  view.items.reserve(matches.size());
  for (auto index = first_changed_match_row; index < matches.size(); ++index)
  {
    auto const& match = matches[index];
    // The controller performs literal filtering. An exact duplicate query in
    // every value keeps the select-list chrome's generic filter stable and
    // preserves chronological order without exposing an alternate search path.
    view.items.push_back(SelectListItemView{.value = std::string(query),
                                            .label = match.identity,
                                            .description = {},
                                            .group = {},
                                            .detail = match.detail,
                                            .badge = {},
                                            .current = false,
                                            .enabled = true,
                                            .disabled_reason = {}});
    ++modal_row_build_count;
  }
  view.subtitle = std::to_string(matches.size()) + (matches.size() == 1 ? " item" : " items");
  view.query = query;
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
  static_cast<void>(projection_cache_.update_query(query_));
  auto const update = projection_cache_.rebuild_all(snapshot, renderer_.transcript_layout_cache.layout);
  rebuild(std::nullopt, std::numeric_limits<std::size_t>::max(), update.first_changed_match_row);
  snapshot.status = projection_cache_.matches().empty() ? "no transcript matches" : "transcript search opened";
  return true;
}

void TranscriptSearchController::rebuild(std::optional<std::size_t> selected_item_index, std::size_t fallback_selection, std::size_t first_changed_match_row)
{
  auto& snapshot = presentation_state_.snapshot;
  auto const& matches = projection_cache_.matches();
  if (!snapshot.select_list)
  {
    snapshot.select_list = SelectListView{
        .title = "Transcript search",
        .subtitle = {},
        .items = {},
        .selected_item_index = 0,
        .query = query_,
        .placeholder = "Literal query (empty lists all)",
        .empty_text = "No transcript matches",
        .footer_hint = "Enter jump · Esc restore",
    };
    first_changed_match_row = 0;
  }
  auto& view = *snapshot.select_list;
  detail::update_transcript_search_select_list_rows(view, matches, query_, first_changed_match_row, modal_row_build_count_);
  view.selected_item_index = 0;
  if (!matches.empty())
  {
    auto selected = matches.end();
    if (selected_item_index)
      selected = std::ranges::find_if(matches, [&](auto const& match) { return match.item_index == *selected_item_index; });
    if (selected != matches.end())
      view.selected_item_index = static_cast<std::size_t>(selected - matches.begin());
    else if (fallback_selection == std::numeric_limits<std::size_t>::max())
      view.selected_item_index = matches.size() - 1;
    else
      view.selected_item_index = std::min(fallback_selection, matches.size() - 1);
  }
}

void TranscriptSearchController::refresh_after_transcript_mutation(std::ptrdiff_t item_index_shift, std::size_t changed_from_item_index)
{
  if (!is_open())
    return;
  std::optional<std::size_t> selected_item_index;
  auto fallback_selection = std::size_t{0};
  if (presentation_state_.snapshot.select_list)
  {
    fallback_selection = presentation_state_.snapshot.select_list->selected_item_index;
    auto const& matches = projection_cache_.matches();
    if (fallback_selection < matches.size())
      selected_item_index = detail::shift_transcript_search_item_index(matches[fallback_selection].item_index, item_index_shift);
  }
  accumulated_item_index_shift_ += item_index_shift;
  refresh_authoritative_layout();
  auto const update = projection_cache_.refresh_after_transcript_mutation(presentation_state_.snapshot, renderer_.transcript_layout_cache.layout,
                                                                          item_index_shift, changed_from_item_index);
  rebuild(selected_item_index, fallback_selection, update.first_changed_match_row);
}

void TranscriptSearchController::refresh_after_resize()
{
  if (!is_open())
    return;
  std::optional<std::size_t> selected_item_index;
  auto fallback_selection = std::size_t{0};
  if (presentation_state_.snapshot.select_list)
  {
    fallback_selection = presentation_state_.snapshot.select_list->selected_item_index;
    auto const& matches = projection_cache_.matches();
    if (fallback_selection < matches.size())
      selected_item_index = matches[fallback_selection].item_index;
  }
  refresh_authoritative_layout();
  auto const update = projection_cache_.rebuild_all(presentation_state_.snapshot, renderer_.transcript_layout_cache.layout);
  rebuild(selected_item_index, fallback_selection, update.first_changed_match_row);
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
  projection_cache_.clear();
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
    projection_cache_.clear();
    query_.clear();
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
    auto const& matches = projection_cache_.matches();
    if (input_result.selected_item_index < matches.size())
    {
      auto const item_index = matches[input_result.selected_item_index].item_index;
      snapshot.select_list.reset();
      active_select_list_ = ActiveSelectList::None;
      projection_cache_.clear();
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
      auto const& matches = projection_cache_.matches();
      if (input_result.selected_item_index < matches.size())
        selected_item_index = matches[input_result.selected_item_index].item_index;
    }
    else if (snapshot.select_list->selected_item_index < projection_cache_.matches().size())
    {
      selected_item_index = projection_cache_.matches()[snapshot.select_list->selected_item_index].item_index;
    }
    auto first_changed_match_row = projection_cache_.matches().size();
    if (detail::transcript_search_query_valid(input_result.query))
    {
      query_ = std::move(input_result.query);
      first_changed_match_row = projection_cache_.update_query(query_).first_changed_match_row;
    }
    else
    {
      snapshot.status = "search query is limited to 1024 printable bytes";
    }
    rebuild(selected_item_index, input_result.selected_item_index, first_changed_match_row);
    return renderer_.request_render();
  }
  return true;
}

void TranscriptSearchController::close_before_prompt()
{
  if (is_open())
    restore_saved_viewport({});
}

detail::TranscriptSearchDiagnostics TranscriptSearchController::diagnostics() const noexcept
{
  return detail::TranscriptSearchDiagnostics{.projection_build_count = projection_cache_.projection_build_count(),
                                             .match_scan_count = projection_cache_.match_scan_count(),
                                             .modal_row_build_count = modal_row_build_count_};
}

}  // namespace ava::tui
