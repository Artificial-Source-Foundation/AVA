#include "sys.h"
#include "ava/tui/composer.h"
#include "ava/tui/composer_internal.h"

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace ava::tui {
namespace {

std::string workspace_surface(std::string text, std::size_t width)
{
  return detail::screen_surface_line(detail::fit_line_preserving_sgr(std::move(text), width), width);
}

void append_wrapped(std::vector<std::string>& lines, std::string_view text, std::size_t width, std::string_view prefix = {})
{
  auto const prefix_columns = detail::terminal_text_columns(prefix);
  auto const body_width = width > prefix_columns ? width - prefix_columns : std::size_t{1};
  auto wrapped = detail::wrap_transcript_text(sanitize_terminal_text(text), body_width);
  if (wrapped.empty())
    wrapped.emplace_back();
  for (auto& line : wrapped) lines.push_back(std::string(prefix) + std::move(line));
}

std::vector<std::string> workspace_body(SubagentWorkspaceView const& view, std::size_t width)
{
  std::vector<std::string> lines;
  auto append_notice = [&](std::string text, std::string_view color = detail::kSgrMuted) {
    if (text.empty())
      return;
    auto const first = lines.size();
    append_wrapped(lines, text, width);
    for (auto index = first; index < lines.size(); ++index) lines[index] = std::string(color) + std::move(lines[index]) + std::string(detail::kSgrReset);
  };

  append_notice(view.notice, detail::kSgrWarning);
  if (view.evicted)
    append_notice(view.messages.empty() ? "Subagent job is no longer retained." : "Subagent job is no longer retained; showing the last committed messages.",
                  detail::kSgrWarning);
  else if (view.unavailable)
    append_notice(view.messages.empty() ? "Subagent workspace unavailable." : "Subagent unavailable; showing the last committed messages.",
                  detail::kSgrWarning);
  else if (view.refresh_unavailable)
    append_notice("Live refresh unavailable; showing the last committed messages.", detail::kSgrWarning);
  else if (view.freeze_pending)
    append_notice("Final refresh pending; showing the last committed messages.");

  if (!view.launch_detail.empty())
  {
    auto const first = lines.size();
    append_wrapped(lines, view.launch_detail, width, "Launch: ");
    for (auto index = first; index < lines.size(); ++index)
      lines[index] = std::string(detail::kSgrMuted) + std::move(lines[index]) + std::string(detail::kSgrReset);
  }

  if (!lines.empty() && !view.messages.empty())
    lines.emplace_back();

  for (std::size_t index = 0; index < view.messages.size(); ++index)
  {
    auto const& message = view.messages[index];
    auto const role = message.role == ava::agent::SubagentLiveMessageRole::User ? std::string_view("User") : std::string_view("Assistant");
    auto const color = message.role == ava::agent::SubagentLiveMessageRole::User ? detail::kSgrAccent : detail::kSgrSuccess;
    lines.push_back(std::string(color) + std::string(detail::kSgrBold) + std::string(role) + std::string(detail::kSgrReset));
    append_wrapped(lines, message.text, width, "  ");
    if (index + 1 < view.messages.size())
      lines.emplace_back();
  }

  if (view.messages.empty() && lines.empty())
  {
    if (view.terminal)
      append_notice("No committed messages.");
    else
      append_notice("Waiting for committed messages…");
  }
  return lines;
}

std::size_t workspace_body_height(std::size_t height)
{
  return height > 2 ? height - 2 : std::size_t{0};
}

}  // namespace

std::size_t subagent_workspace_max_scroll_offset(SubagentWorkspaceView const& view, std::size_t width, std::size_t height)
{
  auto const body = workspace_body(view, std::max<std::size_t>(width, 1));
  auto const viewport = workspace_body_height(height);
  return body.size() > viewport ? body.size() - viewport : std::size_t{0};
}

std::vector<std::string> render_subagent_workspace(SubagentWorkspaceView const& view, std::size_t width, std::size_t height)
{
  width = std::max<std::size_t>(width, 1);
  std::vector<std::string> frame;
  frame.reserve(height);
  if (height == 0)
    return frame;

  std::string header = std::string(detail::kSgrBold) +
                       sanitize_terminal_text(view.title.empty() ? std::string_view("Subagent") : std::string_view(view.title)) +
                       std::string(detail::kSgrReset);
  if (view.evicted)
    header += "  " + std::string(detail::kSgrWarning) + "No longer retained" + std::string(detail::kSgrReset);
  else if (view.unavailable)
    header += "  " + std::string(detail::kSgrWarning) + "Unavailable" + std::string(detail::kSgrReset);
  else if (view.refresh_unavailable)
    header += "  " + std::string(detail::kSgrWarning) + "Live refresh unavailable" + std::string(detail::kSgrReset);
  else if (view.freeze_pending)
    header += "  " + std::string(detail::kSgrMuted) + "Final refresh pending" + std::string(detail::kSgrReset);
  if (!view.status.empty())
    header += "  " + std::string(detail::kSgrMuted) + sanitize_terminal_text(view.status) + std::string(detail::kSgrReset);
  frame.push_back(workspace_surface(std::move(header), width));
  if (height == 1)
    return frame;

  auto const body = workspace_body(view, width);
  auto const viewport = workspace_body_height(height);
  auto const max_scroll = body.size() > viewport ? body.size() - viewport : std::size_t{0};
  auto const start = std::min(view.scroll_offset, max_scroll);
  for (std::size_t row = 0; row < viewport; ++row)
  {
    auto const index = start + row;
    frame.push_back(workspace_surface(index < body.size() ? body[index] : std::string{}, width));
  }

  std::string footer;
  if (view.terminal)
  {
    if (width >= 56)
      footer = "Esc jobs · Tab/Shift+Tab cycle · ↑↓ scroll";
    else if (width >= 34)
      footer = "Esc jobs · Tab cycle · ↑↓ scroll";
    else
      footer = "Esc · Tab · ↑↓";
  }
  else if (width >= 72)
    footer = "Esc jobs · Tab/Shift+Tab cycle · C cancel · P promote · ↑↓ scroll";
  else if (width >= 46)
    footer = "Esc jobs · Tab cycle · C cancel · P promote · ↑↓ scroll";
  else
    footer = "Esc · Tab · C · P · ↑↓";
  frame.push_back(workspace_surface(std::string(detail::kSgrMuted) + footer + std::string(detail::kSgrReset), width));
  return frame;
}

}  // namespace ava::tui
