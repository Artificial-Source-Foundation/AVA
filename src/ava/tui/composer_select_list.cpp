#include "sys.h"
#include "ava/tui/composer_internal.h"
#include "ava/tui/keybindings.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <optional>
#include <utility>

namespace ava::tui {
namespace {

std::string lower_ascii(std::string_view text)
{
  std::string lowered;
  lowered.reserve(text.size());
  for (char const ch : text) lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
  return lowered;
}

std::optional<int> fuzzy_match_score(std::string_view query, std::string_view candidate)
{
  if (query.empty())
    return 0;
  if (candidate.empty())
    return std::nullopt;

  auto const lowered_query = lower_ascii(query);
  auto const lowered_candidate = lower_ascii(candidate);
  auto const contains = lowered_candidate.find(lowered_query);
  if (contains != std::string::npos)
  {
    auto const length_delta = lowered_candidate.size() > lowered_query.size() ? lowered_candidate.size() - lowered_query.size() : std::size_t{0};
    auto const max_score = static_cast<std::size_t>(std::numeric_limits<int>::max());
    auto const capped_delta = std::min<std::size_t>(length_delta, 64);
    if (contains > (max_score - capped_delta) / 4)
      return std::numeric_limits<int>::max();
    return static_cast<int>((contains * 4) + capped_delta);
  }

  std::size_t query_index = 0;
  std::size_t previous_match = std::string::npos;
  int gap_penalty = 0;
  for (std::size_t index = 0; index < lowered_candidate.size() && query_index < lowered_query.size(); ++index)
  {
    if (lowered_candidate[index] != lowered_query[query_index])
      continue;
    if (previous_match != std::string::npos)
    {
      gap_penalty += static_cast<int>(std::min<std::size_t>(index - previous_match - 1, 32));
    }
    previous_match = index;
    ++query_index;
  }

  if (query_index != lowered_query.size())
    return std::nullopt;
  return 1000 + gap_penalty + static_cast<int>(std::min<std::size_t>(lowered_candidate.size(), 128));
}

std::optional<int> item_match_score(SelectListView const& view, SelectListItemView const& item)
{
  if (view.query.empty())
    return 0;
  std::optional<int> best;
  for (auto const field : {std::string_view(item.label), std::string_view(item.description), std::string_view(item.value), std::string_view(item.group),
                           std::string_view(item.detail), std::string_view(item.badge)})
  {
    auto score = fuzzy_match_score(view.query, field);
    if (!score)
      continue;
    if (!best || *score < *best)
      best = *score;
  }
  return best;
}

std::string select_modal_line(std::string content, std::size_t width)
{
  return detail::composer_surface_line("  " + std::move(content), width);
}

std::string select_title_line(SelectListView const& view, std::size_t width)
{
  auto title = view.title.empty() ? std::string("Select") : sanitize_terminal_text(view.title);
  std::string line = std::string(detail::kSgrBold) + title + std::string(detail::kSgrReset) + std::string(detail::kSgrComposerBg);
  std::string const esc = std::string(detail::kSgrMuted) + "esc" + std::string(detail::kSgrReset) + std::string(detail::kSgrComposerBg);
  auto const line_cols = detail::terminal_text_columns(line);
  auto const esc_cols = detail::terminal_text_columns(esc);
  if (line_cols + esc_cols + 3 < width)
    line += std::string(width - line_cols - esc_cols - 2, ' ') + esc;
  return select_modal_line(std::move(line), width);
}

std::vector<std::string> select_wrapped_lines(std::string_view text, std::size_t width)
{
  std::vector<std::string> lines;
  auto const content_width = width > 4 ? width - 4 : width;
  auto const sanitized = sanitize_terminal_text(text);
  for (auto const& raw_line : split_lines(sanitized))
  {
    for (auto const& wrapped : detail::wrap_transcript_text(raw_line, content_width))
    {
      lines.push_back(select_modal_line(wrapped, width));
    }
  }
  if (lines.empty())
    lines.push_back(select_modal_line("", width));
  return lines;
}

std::string select_search_line(SelectListView const& view, std::size_t width)
{
  std::string query = sanitize_terminal_text(view.query);
  if (query.empty())
  {
    query = std::string(detail::kSgrMuted) + sanitize_terminal_text(view.placeholder) + std::string(detail::kSgrReset) + std::string(detail::kSgrComposerBg);
  }
  query += std::string(detail::kSgrAccent) + "█" + std::string(detail::kSgrReset) + std::string(detail::kSgrComposerBg);
  return select_modal_line("Search: " + std::move(query), width);
}

std::string select_group_line(std::string_view group, std::size_t width)
{
  return select_modal_line(
      std::string(detail::kSgrWarning) + sanitize_terminal_text(group) + std::string(detail::kSgrReset) + std::string(detail::kSgrComposerBg), width);
}

std::string select_item_line(SelectListItemView const& item, bool selected, std::size_t width)
{
  std::string line = selected ? "› " : "  ";
  line += item.current ? "● " : "  ";
  line += sanitize_terminal_text(item.label.empty() ? item.value : item.label);
  if (item.current)
    line += "  current";
  if (!item.badge.empty())
    line += "  " + sanitize_terminal_text(item.badge);
  if (!item.description.empty())
  {
    line +=
        "  " + std::string(detail::kSgrMuted) + sanitize_terminal_text(item.description) + std::string(detail::kSgrReset) + std::string(detail::kSgrComposerBg);
  }
  if (!item.detail.empty())
  {
    line += "  " + std::string(detail::kSgrMuted) + sanitize_terminal_text(item.detail) + std::string(detail::kSgrReset) + std::string(detail::kSgrComposerBg);
  }
  if (!item.enabled)
  {
    line += "  " + std::string(detail::kSgrWarning) + "disabled";
    if (!item.disabled_reason.empty())
      line += ": " + sanitize_terminal_text(item.disabled_reason);
    line += std::string(detail::kSgrReset) + std::string(detail::kSgrComposerBg);
  }
  line = detail::fit_line_preserving_sgr(std::move(line), width > 4 ? width - 4 : width);
  if (selected)
    line = std::string(detail::kReverseVideo) + line + std::string(detail::kSgrReset) + std::string(detail::kSgrComposerBg);
  if (!item.enabled && !selected)
    line = std::string(detail::kSgrDim) + line + std::string(detail::kSgrReset) + std::string(detail::kSgrComposerBg);
  return select_modal_line(std::move(line), width);
}

std::string select_footer_line(SelectListView const& view, std::size_t width)
{
  auto hint = view.footer_hint.empty() ? std::string("↑/↓ select  PgUp/PgDn page  Enter confirm  Type to search  Esc cancel")
                                       : sanitize_terminal_text(view.footer_hint);
  return select_modal_line(std::string(detail::kSgrMuted) + std::move(hint) + std::string(detail::kSgrReset), width);
}

struct SelectListContentRow
{
  std::optional<std::size_t> item_index;
  std::string group;
};

std::vector<SelectListContentRow> select_list_content_rows(SelectListView const& view, std::size_t row_budget)
{
  std::vector<SelectListContentRow> rows;
  if (row_budget == 0)
    return rows;

  auto const matches = filter_select_list_items(view);
  if (matches.empty())
    return rows;
  auto const selected = clamp_select_list_selection(view, view.selected_item_index);
  auto const selected_it = std::ranges::find(matches, selected);
  auto const selected_visible = selected_it == matches.end() ? std::size_t{0} : static_cast<std::size_t>(selected_it - matches.begin());

  auto build_rows = [&](std::size_t start) {
    std::vector<SelectListContentRow> candidate_rows;
    candidate_rows.reserve(row_budget);
    std::string last_group;
    for (std::size_t visible = start; visible < matches.size() && candidate_rows.size() < row_budget; ++visible)
    {
      auto const item_index = matches[visible];
      auto const& item = view.items[item_index];
      if (!item.group.empty() && item.group != last_group)
      {
        auto const rows_remaining = row_budget - candidate_rows.size();
        if (rows_remaining >= 2)
        {
          candidate_rows.push_back(SelectListContentRow{.item_index = std::nullopt, .group = item.group});
        }
        else if (row_budget > 1)
        {
          break;
        }
      }
      if (candidate_rows.size() >= row_budget)
        break;
      candidate_rows.push_back(SelectListContentRow{.item_index = item_index, .group = {}});
      last_group = item.group;
    }
    return candidate_rows;
  };

  auto start = selected_visible >= row_budget ? selected_visible - row_budget + 1 : std::size_t{0};
  for (;;)
  {
    rows = build_rows(start);
    if (std::ranges::any_of(rows, [selected](SelectListContentRow const& row) { return row.item_index == selected; }) || start >= selected_visible)
    {
      return rows;
    }
    ++start;
  }
}

std::string character_text(InputEvent const& event)
{
  if (!event.text.empty())
    return event.text;
  if (event.character == '\0')
    return {};
  return std::string(1, event.character);
}

bool event_matches_action(InputEvent const& event, TuiKeyBindings const& bindings, TuiAction action)
{
  if (key_matches_action(bindings, action, event.key))
    return true;
  if (event.key == Key::Character && character_text(event) == "L")
    return key_matches_action(bindings, action, Key::ShiftL);
  if (event.key == Key::Character && character_text(event) == "T")
    return key_matches_action(bindings, action, Key::ShiftT);
  return false;
}

InputEvent select_list_bound_event(InputEvent event, TuiKeyBindings const& bindings)
{
  auto bound = [&](TuiAction action) { return event_matches_action(event, bindings, action); };
  if (bound(TuiAction::SelectConfirm))
    return InputEvent{.key = Key::Enter};
  if (bound(TuiAction::SelectCancel))
    return InputEvent{.key = Key::Escape};
  if (bound(TuiAction::SelectPrev))
    return InputEvent{.key = Key::ArrowUp};
  if (bound(TuiAction::SelectNext))
    return InputEvent{.key = Key::ArrowDown};
  if (bound(TuiAction::SelectPageUp))
    return InputEvent{.key = Key::PageUp};
  if (bound(TuiAction::SelectPageDown))
    return InputEvent{.key = Key::PageDown};
  if (bound(TuiAction::SessionTogglePath))
    return InputEvent{.key = Key::CtrlP};
  if (bound(TuiAction::SessionToggleSort))
    return InputEvent{.key = Key::CtrlS};
  if (bound(TuiAction::SessionToggleNamedFilter))
    return InputEvent{.key = Key::CtrlN};
  if (bound(TuiAction::SessionRename))
    return InputEvent{.key = Key::CtrlR};
  if (bound(TuiAction::SessionArchive))
    return InputEvent{.key = Key::CtrlD};
  if (bound(TuiAction::SessionArchiveNoninvasive))
    return InputEvent{.key = Key::CtrlBackspace};
  if (bound(TuiAction::TreeFoldOrUp))
    return InputEvent{.key = Key::CtrlArrowLeft};
  if (bound(TuiAction::TreeUnfoldOrDown))
    return InputEvent{.key = Key::CtrlArrowRight};
  if (bound(TuiAction::TreeEditLabel))
    return InputEvent{.key = Key::CtrlL};
  if (bound(TuiAction::TreeToggleLabelTimestamp))
    return InputEvent{.key = Key::ShiftT};
  if (bound(TuiAction::TreeFilterLabeledOnly))
    return InputEvent{.key = Key::CtrlN};
  if (bound(TuiAction::TreeFilterAll))
    return InputEvent{.key = Key::CtrlA};
  return event;
}

}  // namespace

std::vector<std::size_t> filter_select_list_items(SelectListView const& view)
{
  if (view.query.empty())
  {
    std::vector<std::size_t> indices;
    indices.reserve(view.items.size());
    for (std::size_t index = 0; index < view.items.size(); ++index) indices.push_back(index);
    return indices;
  }

  std::vector<std::pair<int, std::size_t>> scored;
  scored.reserve(view.items.size());
  for (std::size_t index = 0; index < view.items.size(); ++index)
  {
    auto score = item_match_score(view, view.items[index]);
    if (score)
      scored.push_back({*score, index});
  }
  std::ranges::sort(scored, [](auto const& lhs, auto const& rhs) {
    if (lhs.first != rhs.first)
      return lhs.first < rhs.first;
    return lhs.second < rhs.second;
  });

  std::vector<std::size_t> indices;
  indices.reserve(scored.size());
  for (auto const& [_, index] : scored) indices.push_back(index);
  return indices;
}

std::size_t clamp_select_list_selection(SelectListView const& view, std::size_t selected_index)
{
  auto const matches = filter_select_list_items(view);
  if (matches.empty())
    return 0;
  if (std::ranges::find(matches, selected_index) != matches.end())
    return selected_index;
  return matches.front();
}

std::size_t previous_select_list_selection(SelectListView const& view, std::size_t selected_index)
{
  auto const matches = filter_select_list_items(view);
  if (matches.empty())
    return 0;
  auto const selected = clamp_select_list_selection(view, selected_index);
  auto const current = std::ranges::find(matches, selected);
  auto visible = current == matches.end() ? std::size_t{0} : static_cast<std::size_t>(current - matches.begin());
  visible = visible == 0 ? matches.size() - 1 : visible - 1;
  return matches[visible];
}

std::size_t next_select_list_selection(SelectListView const& view, std::size_t selected_index)
{
  auto const matches = filter_select_list_items(view);
  if (matches.empty())
    return 0;
  auto const selected = clamp_select_list_selection(view, selected_index);
  auto const current = std::ranges::find(matches, selected);
  auto visible = current == matches.end() ? std::size_t{0} : static_cast<std::size_t>(current - matches.begin());
  visible = (visible + 1) % matches.size();
  return matches[visible];
}

std::size_t page_select_list_selection(SelectListView const& view, std::size_t selected_index, bool previous, std::size_t rows)
{
  auto const matches = filter_select_list_items(view);
  if (matches.empty())
    return 0;
  auto const selected = clamp_select_list_selection(view, selected_index);
  auto const current = std::ranges::find(matches, selected);
  auto visible = current == matches.end() ? std::size_t{0} : static_cast<std::size_t>(current - matches.begin());
  if (previous)
  {
    visible = rows > visible ? std::size_t{0} : visible - rows;
  }
  else
  {
    visible = std::min(matches.size() - 1, visible + rows);
  }
  return matches[visible];
}

SelectListInputResult handle_select_list_input(SelectListView const& view, InputEvent event)
{
  SelectListInputResult result{
      .selected_item_index = clamp_select_list_selection(view, view.selected_item_index), .query = view.query, .action = SelectListInputAction::None};
  auto view_with_result = [&]() {
    auto current = view;
    current.query = result.query;
    current.selected_item_index = result.selected_item_index;
    return current;
  };

  switch (event.key)
  {
    case Key::Character: {
      if (auto text = character_text(event); !text.empty())
      {
        result.query += text;
        auto current = view_with_result();
        result.selected_item_index = clamp_select_list_selection(current, result.selected_item_index);
        result.action = SelectListInputAction::Redraw;
      }
      return result;
    }
    case Key::Space:
      result.query += ' ';
      {
        auto current = view_with_result();
        result.selected_item_index = clamp_select_list_selection(current, result.selected_item_index);
        result.action = SelectListInputAction::Redraw;
      }
      return result;
    case Key::Backspace:
    case Key::ShiftBackspace:
      if (!result.query.empty())
      {
        erase_last_utf8_codepoint(result.query);
        auto current = view_with_result();
        result.selected_item_index = clamp_select_list_selection(current, result.selected_item_index);
        result.action = SelectListInputAction::Redraw;
      }
      return result;
    case Key::ArrowUp:
    case Key::MouseWheelUp: {
      auto current = view_with_result();
      result.selected_item_index = previous_select_list_selection(current, result.selected_item_index);
      result.action = SelectListInputAction::Redraw;
      return result;
    }
    case Key::ArrowDown:
    case Key::Tab:
    case Key::MouseWheelDown: {
      auto current = view_with_result();
      result.selected_item_index = next_select_list_selection(current, result.selected_item_index);
      result.action = SelectListInputAction::Redraw;
      return result;
    }
    case Key::Enter: {
      auto current = view_with_result();
      auto const matches = filter_select_list_items(current);
      if (matches.empty())
      {
        result.action = SelectListInputAction::Redraw;
        return result;
      }
      result.selected_item_index = clamp_select_list_selection(current, result.selected_item_index);
      if (result.selected_item_index >= view.items.size() || !view.items[result.selected_item_index].enabled)
      {
        result.action = SelectListInputAction::Redraw;
        return result;
      }
      result.action = SelectListInputAction::Resolve;
      return result;
    }
    case Key::Escape:
    case Key::CtrlC:
      result.action = SelectListInputAction::Cancel;
      return result;
    case Key::CtrlD:
      result.action = SelectListInputAction::Archive;
      return result;
    case Key::CtrlBackspace:
      if (result.query.empty())
        result.action = SelectListInputAction::ArchiveNoninvasive;
      return result;
    case Key::CtrlA:
      result.action = SelectListInputAction::ToggleArchivedFilter;
      return result;
    case Key::ShiftT:
      result.action = SelectListInputAction::ToggleLabelTimestamp;
      return result;
    case Key::PageUp: {
      auto current = view_with_result();
      result.selected_item_index = page_select_list_selection(current, result.selected_item_index, true, 5);
      result.action = SelectListInputAction::Redraw;
      return result;
    }
    case Key::PageDown: {
      auto current = view_with_result();
      result.selected_item_index = page_select_list_selection(current, result.selected_item_index, false, 5);
      result.action = SelectListInputAction::Redraw;
      return result;
    }
    case Key::Home:
    case Key::CtrlHome: {
      auto const matches = filter_select_list_items(view_with_result());
      if (!matches.empty())
      {
        result.selected_item_index = matches.front();
        result.action = SelectListInputAction::Redraw;
      }
      return result;
    }
    case Key::End:
    case Key::CtrlEnd: {
      auto const matches = filter_select_list_items(view_with_result());
      if (!matches.empty())
      {
        result.selected_item_index = matches.back();
        result.action = SelectListInputAction::Redraw;
      }
      return result;
    }
    case Key::CtrlArrowLeft:
    case Key::AltArrowLeft:
      result.action = SelectListInputAction::BranchParent;
      return result;
    case Key::CtrlArrowRight:
    case Key::AltArrowRight:
      result.action = SelectListInputAction::BranchChild;
      return result;
    case Key::ArrowLeft:
    case Key::ArrowRight:
    case Key::ShiftArrowUp:
    case Key::ShiftArrowDown:
    case Key::ShiftArrowLeft:
    case Key::ShiftArrowRight:
    case Key::ShiftCtrlArrowLeft:
    case Key::ShiftCtrlArrowRight:
    case Key::ShiftAltArrowLeft:
    case Key::ShiftAltArrowRight:
    case Key::ShiftHome:
    case Key::ShiftEnd:
    case Key::ShiftCtrlHome:
    case Key::ShiftCtrlEnd:
    case Key::ShiftEnter:
    case Key::CtrlB:
    case Key::CtrlE:
    case Key::CtrlF:
    case Key::CtrlG:
    case Key::CtrlH:
    case Key::CtrlK:
    case Key::CtrlMinus:
    case Key::CtrlN:
      result.action = SelectListInputAction::ToggleNamedFilter;
      return result;
    case Key::CtrlP:
      result.action = SelectListInputAction::TogglePathDisplay;
      return result;
    case Key::CtrlShiftP:
      break;
    case Key::CtrlX:
      result.action = SelectListInputAction::ModelsClearAll;
      return result;
    case Key::CtrlR:
      result.action = SelectListInputAction::Rename;
      return result;
    case Key::CtrlL:
      result.action = SelectListInputAction::Label;
      return result;
    case Key::CtrlS:
    case Key::CtrlT:
      result.action = SelectListInputAction::CycleSort;
      return result;
    case Key::Delete:
    case Key::ShiftDelete:
    case Key::Insert:
    case Key::Clear:
    case Key::ShiftTab:
    case Key::ShiftL:
    case Key::CtrlEnter:
    case Key::AltEnter:
      break;
    case Key::AltArrowUp:
      result.action = SelectListInputAction::ModelsReorderUp;
      return result;
    case Key::AltArrowDown:
      result.action = SelectListInputAction::ModelsReorderDown;
      return result;
    case Key::CtrlU:
    case Key::CtrlV:
    case Key::CtrlW:
    case Key::CtrlY:
    case Key::CtrlZ:
    case Key::CtrlSpace:
    case Key::CtrlSlash:
    case Key::Ctrl0:
    case Key::Ctrl1:
    case Key::Ctrl2:
    case Key::Ctrl3:
    case Key::Ctrl4:
    case Key::Ctrl5:
    case Key::Ctrl6:
    case Key::Ctrl7:
    case Key::Ctrl8:
    case Key::Ctrl9:
    case Key::CtrlRightBracket:
    case Key::CtrlO:
    case Key::AltBackspace:
    case Key::AltB:
    case Key::AltD:
    case Key::AltDelete:
    case Key::AltF:
    case Key::AltH:
    case Key::AltJ:
    case Key::AltK:
    case Key::AltL:
    case Key::AltW:
    case Key::CtrlAltRightBracket:
    case Key::AltY:
    case Key::MouseLeftClick:
    case Key::MouseLeftDrag:
    case Key::MouseLeftRelease:
    case Key::F1:
    case Key::F2:
    case Key::F3:
    case Key::F4:
    case Key::F5:
    case Key::F6:
    case Key::F7:
    case Key::F8:
    case Key::F9:
    case Key::F10:
    case Key::F11:
    case Key::F12:
    case Key::Unknown:
      break;
  }
  return result;
}

SelectListInputResult handle_select_list_input(SelectListView const& view, InputEvent event, TuiKeyBindings const& bindings)
{
  return handle_select_list_input(view, select_list_bound_event(event, bindings));
}

namespace detail {
namespace {

std::vector<std::string> select_list_modal_prefix(SelectListView const& view, std::size_t width, std::size_t max_lines)
{
  std::vector<std::string> lines;
  if (max_lines == 0)
    return lines;

  lines.push_back(composer_surface_line("", width));
  if (lines.size() >= max_lines)
    return lines;
  lines.push_back(select_title_line(view, width));
  if (lines.size() >= max_lines)
    return lines;

  constexpr std::size_t kRowsAfterSubtitle = 6;  // spacer, search, spacer, one item, spacer, footer
  if (!view.subtitle.empty())
  {
    for (auto const& line : select_wrapped_lines(view.subtitle, width))
    {
      if (lines.size() + 1 + kRowsAfterSubtitle > max_lines)
        break;
      lines.push_back(line);
    }
  }
  if (lines.size() >= max_lines)
    return lines;
  lines.push_back(composer_surface_line("", width));
  if (lines.size() >= max_lines)
    return lines;
  lines.push_back(select_search_line(view, width));
  if (lines.size() >= max_lines)
    return lines;
  lines.push_back(composer_surface_line("", width));
  return lines;
}

}  // namespace

std::optional<std::size_t> select_list_item_for_modal_row(SelectListView const& view, std::size_t modal_row, std::size_t width, std::size_t max_lines)
{
  auto const prefix = select_list_modal_prefix(view, width, max_lines);
  auto const content_start = prefix.size();
  if (modal_row < content_start)
    return std::nullopt;

  auto const reserved_footer = std::size_t{2};
  auto const budget = max_lines > content_start + reserved_footer ? max_lines - content_start - reserved_footer : 0;
  auto const content_rows = select_list_content_rows(view, budget);
  auto const content_row = modal_row - content_start;
  if (content_row >= content_rows.size())
    return std::nullopt;
  return content_rows[content_row].item_index;
}

std::vector<std::string> render_select_list_modal(SelectListView const& view, std::size_t width, std::size_t max_lines)
{
  auto lines = select_list_modal_prefix(view, width, max_lines);
  if (lines.size() >= max_lines)
    return lines;

  auto const matches = filter_select_list_items(view);
  auto const selected = clamp_select_list_selection(view, view.selected_item_index);
  auto const reserved_footer = std::size_t{2};
  auto const budget = max_lines > lines.size() + reserved_footer ? max_lines - lines.size() - reserved_footer : 0;
  auto const content_rows = select_list_content_rows(view, budget);

  if (matches.empty())
  {
    if (lines.size() + reserved_footer < max_lines)
    {
      auto empty = view.empty_text.empty() ? std::string("No matches") : sanitize_terminal_text(view.empty_text);
      lines.push_back(select_modal_line(std::string(kSgrMuted) + std::move(empty) + std::string(kSgrReset), width));
    }
  }
  else
  {
    for (auto const& row : content_rows)
    {
      if (row.item_index)
      {
        auto const item_index = *row.item_index;
        lines.push_back(select_item_line(view.items[item_index], item_index == selected, width));
      }
      else
      {
        lines.push_back(select_group_line(row.group, width));
      }
    }
  }

  if (lines.size() < max_lines)
    lines.push_back(composer_surface_line("", width));
  if (lines.size() < max_lines)
    lines.push_back(select_footer_line(view, width));
  while (lines.size() < max_lines) lines.push_back(composer_surface_line("", width));
  return lines;
}

}  // namespace detail

}  // namespace ava::tui
