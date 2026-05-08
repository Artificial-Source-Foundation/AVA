#include "ava/tui/composer_internal.h"

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
  if (query.empty()) return 0;
  if (candidate.empty()) return std::nullopt;

  auto const lowered_query = lower_ascii(query);
  auto const lowered_candidate = lower_ascii(candidate);
  auto const contains = lowered_candidate.find(lowered_query);
  if (contains != std::string::npos) {
    auto const length_delta = lowered_candidate.size() > lowered_query.size()
                                  ? lowered_candidate.size() - lowered_query.size()
                                  : std::size_t{0};
    auto const max_score = static_cast<std::size_t>(std::numeric_limits<int>::max());
    auto const capped_delta = std::min<std::size_t>(length_delta, 64);
    if (contains > (max_score - capped_delta) / 4) return std::numeric_limits<int>::max();
    return static_cast<int>((contains * 4) + capped_delta);
  }

  std::size_t query_index = 0;
  std::size_t previous_match = std::string::npos;
  int gap_penalty = 0;
  for (std::size_t index = 0; index < lowered_candidate.size() && query_index < lowered_query.size(); ++index) {
    if (lowered_candidate[index] != lowered_query[query_index]) continue;
    if (previous_match != std::string::npos) {
      gap_penalty += static_cast<int>(std::min<std::size_t>(index - previous_match - 1, 32));
    }
    previous_match = index;
    ++query_index;
  }

  if (query_index != lowered_query.size()) return std::nullopt;
  return 1000 + gap_penalty + static_cast<int>(std::min<std::size_t>(lowered_candidate.size(), 128));
}

std::optional<int> item_match_score(SelectListView const& view, SelectListItemView const& item)
{
  if (view.query.empty()) return 0;
  std::optional<int> best;
  for (auto const field :
       {std::string_view(item.label), std::string_view(item.description), std::string_view(item.value),
        std::string_view(item.group), std::string_view(item.detail), std::string_view(item.badge)}) {
    auto score = fuzzy_match_score(view.query, field);
    if (!score) continue;
    if (!best || *score < *best) best = *score;
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
  std::string line =
      std::string(detail::kSgrBold) + title + std::string(detail::kSgrReset) + std::string(detail::kSgrComposerBg);
  std::string const esc =
      std::string(detail::kSgrMuted) + "esc" + std::string(detail::kSgrReset) + std::string(detail::kSgrComposerBg);
  auto const line_cols = detail::terminal_text_columns(line);
  auto const esc_cols = detail::terminal_text_columns(esc);
  if (line_cols + esc_cols + 3 < width) line += std::string(width - line_cols - esc_cols - 2, ' ') + esc;
  return select_modal_line(std::move(line), width);
}

std::vector<std::string> select_wrapped_lines(std::string_view text, std::size_t width)
{
  std::vector<std::string> lines;
  auto const content_width = width > 4 ? width - 4 : width;
  auto const sanitized = sanitize_terminal_text(text);
  for (auto const& raw_line : split_lines(sanitized)) {
    for (auto const& wrapped : detail::wrap_transcript_text(raw_line, content_width)) {
      lines.push_back(select_modal_line(wrapped, width));
    }
  }
  if (lines.empty()) lines.push_back(select_modal_line("", width));
  return lines;
}

std::string select_search_line(SelectListView const& view, std::size_t width)
{
  std::string query = sanitize_terminal_text(view.query);
  if (query.empty()) {
    query = std::string(detail::kSgrMuted) + sanitize_terminal_text(view.placeholder) + std::string(detail::kSgrReset) +
            std::string(detail::kSgrComposerBg);
  }
  query += std::string(detail::kSgrAccent) + "█" + std::string(detail::kSgrReset) + std::string(detail::kSgrComposerBg);
  return select_modal_line("Search: " + std::move(query), width);
}

std::string select_group_line(std::string_view group, std::size_t width)
{
  return select_modal_line(std::string(detail::kSgrWarning) + sanitize_terminal_text(group) +
                               std::string(detail::kSgrReset) + std::string(detail::kSgrComposerBg),
                           width);
}

std::string select_item_line(SelectListItemView const& item, bool selected, std::size_t width)
{
  std::string line = selected ? "› " : "  ";
  line += item.current ? "● " : "  ";
  line += sanitize_terminal_text(item.label.empty() ? item.value : item.label);
  if (item.current) line += "  current";
  if (!item.badge.empty()) line += "  " + sanitize_terminal_text(item.badge);
  if (!item.description.empty()) {
    line += "  " + std::string(detail::kSgrMuted) + sanitize_terminal_text(item.description) +
            std::string(detail::kSgrReset) + std::string(detail::kSgrComposerBg);
  }
  if (!item.detail.empty()) {
    line += "  " + std::string(detail::kSgrMuted) + sanitize_terminal_text(item.detail) +
            std::string(detail::kSgrReset) + std::string(detail::kSgrComposerBg);
  }
  if (!item.enabled) {
    line += "  " + std::string(detail::kSgrWarning) + "disabled";
    if (!item.disabled_reason.empty()) line += ": " + sanitize_terminal_text(item.disabled_reason);
    line += std::string(detail::kSgrReset) + std::string(detail::kSgrComposerBg);
  }
  line = detail::fit_line_preserving_sgr(std::move(line), width > 4 ? width - 4 : width);
  if (selected)
    line = std::string(detail::kReverseVideo) + line + std::string(detail::kSgrReset) +
           std::string(detail::kSgrComposerBg);
  if (!item.enabled && !selected)
    line = std::string(detail::kSgrDim) + line + std::string(detail::kSgrReset) + std::string(detail::kSgrComposerBg);
  return select_modal_line(std::move(line), width);
}

std::string select_footer_line(SelectListView const& view, std::size_t width)
{
  auto hint = view.footer_hint.empty() ? std::string("↑/↓ select  Enter confirm  Type to search  Esc cancel")
                                       : sanitize_terminal_text(view.footer_hint);
  return select_modal_line(std::string(detail::kSgrMuted) + std::move(hint) + std::string(detail::kSgrReset), width);
}

std::string character_text(InputEvent const& event)
{
  if (!event.text.empty()) return event.text;
  if (event.character == '\0') return {};
  return std::string(1, event.character);
}

}  // namespace

std::vector<std::size_t> filter_select_list_items(SelectListView const& view)
{
  if (view.query.empty()) {
    std::vector<std::size_t> indices;
    indices.reserve(view.items.size());
    for (std::size_t index = 0; index < view.items.size(); ++index) indices.push_back(index);
    return indices;
  }

  std::vector<std::pair<int, std::size_t>> scored;
  scored.reserve(view.items.size());
  for (std::size_t index = 0; index < view.items.size(); ++index) {
    auto score = item_match_score(view, view.items[index]);
    if (score) scored.push_back({*score, index});
  }
  std::ranges::sort(scored, [](auto const& lhs, auto const& rhs) {
    if (lhs.first != rhs.first) return lhs.first < rhs.first;
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
  if (matches.empty()) return 0;
  if (std::ranges::find(matches, selected_index) != matches.end()) return selected_index;
  return matches.front();
}

std::size_t previous_select_list_selection(SelectListView const& view, std::size_t selected_index)
{
  auto const matches = filter_select_list_items(view);
  if (matches.empty()) return 0;
  auto const selected = clamp_select_list_selection(view, selected_index);
  auto const current = std::ranges::find(matches, selected);
  auto visible = current == matches.end() ? std::size_t{0} : static_cast<std::size_t>(current - matches.begin());
  visible = visible == 0 ? matches.size() - 1 : visible - 1;
  return matches[visible];
}

std::size_t next_select_list_selection(SelectListView const& view, std::size_t selected_index)
{
  auto const matches = filter_select_list_items(view);
  if (matches.empty()) return 0;
  auto const selected = clamp_select_list_selection(view, selected_index);
  auto const current = std::ranges::find(matches, selected);
  auto visible = current == matches.end() ? std::size_t{0} : static_cast<std::size_t>(current - matches.begin());
  visible = (visible + 1) % matches.size();
  return matches[visible];
}

SelectListInputResult handle_select_list_input(SelectListView const& view, InputEvent event)
{
  SelectListInputResult result{.selected_item_index = clamp_select_list_selection(view, view.selected_item_index),
                               .query = view.query,
                               .action = SelectListInputAction::None};
  auto view_with_result = [&]() {
    auto current = view;
    current.query = result.query;
    current.selected_item_index = result.selected_item_index;
    return current;
  };

  switch (event.key) {
    case Key::Character: {
      if (auto text = character_text(event); !text.empty()) {
        result.query += text;
        auto current = view_with_result();
        result.selected_item_index = clamp_select_list_selection(current, result.selected_item_index);
        result.action = SelectListInputAction::Redraw;
      }
      return result;
    }
    case Key::Backspace:
      if (!result.query.empty()) {
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
      if (matches.empty()) {
        result.action = SelectListInputAction::Redraw;
        return result;
      }
      result.selected_item_index = clamp_select_list_selection(current, result.selected_item_index);
      if (result.selected_item_index >= view.items.size() || !view.items[result.selected_item_index].enabled) {
        result.action = SelectListInputAction::Redraw;
        return result;
      }
      result.action = SelectListInputAction::Resolve;
      return result;
    }
    case Key::Escape:
    case Key::CtrlC:
    case Key::CtrlD:
      result.action = SelectListInputAction::Cancel;
      return result;
    case Key::PageUp: {
      auto current = view_with_result();
      for (int index = 0; index < 5; ++index) {
        result.selected_item_index = previous_select_list_selection(current, result.selected_item_index);
        current.selected_item_index = result.selected_item_index;
      }
      result.action = SelectListInputAction::Redraw;
      return result;
    }
    case Key::PageDown: {
      auto current = view_with_result();
      for (int index = 0; index < 5; ++index) {
        result.selected_item_index = next_select_list_selection(current, result.selected_item_index);
        current.selected_item_index = result.selected_item_index;
      }
      result.action = SelectListInputAction::Redraw;
      return result;
    }
    case Key::ArrowLeft:
    case Key::ArrowRight:
    case Key::MouseLeftClick:
    case Key::ShiftEnter:
    case Key::CtrlA:
    case Key::CtrlB:
    case Key::CtrlE:
    case Key::CtrlF:
    case Key::CtrlK:
    case Key::CtrlR:
    case Key::CtrlT:
    case Key::CtrlU:
    case Key::CtrlW:
    case Key::CtrlY:
    case Key::CtrlZ:
    case Key::AltY:
    case Key::Unknown:
      break;
  }
  return result;
}

namespace detail {

std::vector<std::string> render_select_list_modal(SelectListView const& view, std::size_t width, std::size_t max_lines)
{
  std::vector<std::string> lines;
  if (max_lines == 0) return lines;

  lines.push_back(composer_surface_line("", width));
  if (lines.size() >= max_lines) return lines;
  lines.push_back(select_title_line(view, width));
  if (lines.size() >= max_lines) return lines;

  if (!view.subtitle.empty()) {
    auto const wrapped = select_wrapped_lines(view.subtitle, width);
    for (auto const& line : wrapped) {
      if (lines.size() + 4 >= max_lines) break;
      lines.push_back(line);
    }
  }
  if (lines.size() >= max_lines) return lines;
  lines.push_back(composer_surface_line("", width));
  if (lines.size() >= max_lines) return lines;
  lines.push_back(select_search_line(view, width));
  if (lines.size() >= max_lines) return lines;
  lines.push_back(composer_surface_line("", width));
  if (lines.size() >= max_lines) return lines;

  auto const matches = filter_select_list_items(view);
  auto const selected = clamp_select_list_selection(view, view.selected_item_index);
  auto selected_visible = std::size_t{0};
  if (auto const found = std::ranges::find(matches, selected); found != matches.end()) {
    selected_visible = static_cast<std::size_t>(found - matches.begin());
  }
  auto const reserved_footer = std::size_t{2};
  auto const budget = max_lines > lines.size() + reserved_footer ? max_lines - lines.size() - reserved_footer : 0;
  auto start = budget > 0 && selected_visible >= budget ? selected_visible - budget + 1 : std::size_t{0};
  if (budget > 0 && start + budget > matches.size() && matches.size() > budget) start = matches.size() - budget;

  if (matches.empty()) {
    if (lines.size() + reserved_footer < max_lines) {
      auto empty = view.empty_text.empty() ? std::string("No matches") : sanitize_terminal_text(view.empty_text);
      lines.push_back(select_modal_line(std::string(kSgrMuted) + std::move(empty) + std::string(kSgrReset), width));
    }
  } else {
    std::string last_group;
    for (std::size_t visible = start; visible < matches.size() && lines.size() + reserved_footer < max_lines;
         ++visible) {
      auto const item_index = matches[visible];
      auto const& item = view.items[item_index];
      if (!item.group.empty() && item.group != last_group && lines.size() + reserved_footer + 1 < max_lines) {
        lines.push_back(select_group_line(item.group, width));
        last_group = item.group;
      }
      if (lines.size() + reserved_footer >= max_lines) break;
      lines.push_back(select_item_line(item, item_index == selected, width));
    }
  }

  if (lines.size() < max_lines) lines.push_back(composer_surface_line("", width));
  if (lines.size() < max_lines) lines.push_back(select_footer_line(view, width));
  while (lines.size() < max_lines) lines.push_back(composer_surface_line("", width));
  return lines;
}

}  // namespace detail

}  // namespace ava::tui
