#include "sys.h"
#include "ava/tui/mermaid_projection.h"
#include "ava/tui/text.h"

#include <algorithm>
#include <cctype>
#include <string_view>
#include <utility>

namespace ava::tui::detail {
namespace {

bool ascii_case_equal(std::string_view left, std::string_view right)
{
  if (left.size() != right.size())
    return false;
  for (std::size_t index = 0; index < left.size(); ++index)
  {
    auto const lhs = static_cast<unsigned char>(left[index]);
    auto const rhs = static_cast<unsigned char>(right[index]);
    if (std::tolower(lhs) != std::tolower(rhs))
      return false;
  }
  return true;
}

bool mermaid_info_token(std::string_view line)
{
  if (!line.starts_with("```"))
    return false;
  auto info = line.substr(3);
  while (!info.empty() && (info.front() == ' ' || info.front() == '\t')) info.remove_prefix(1);
  auto const token_end = info.find_first_of(" \t");
  auto const token = info.substr(0, token_end);
  return ascii_case_equal(token, "mermaid");
}

struct SourceLine
{
  std::size_t start = 0;
  std::size_t content_end = 0;
  std::size_t past_end = 0;
};

std::vector<SourceLine> source_lines(std::string_view text)
{
  std::vector<SourceLine> lines;
  std::size_t start = 0;
  for (std::size_t index = 0; index <= text.size(); ++index)
  {
    if (index < text.size() && text[index] != '\n' && text[index] != '\r')
      continue;
    auto const content_end = index;
    if (index < text.size() && text[index] == '\r' && index + 1 < text.size() && text[index + 1] == '\n')
      ++index;
    auto const past_end = index < text.size() ? index + 1 : index;
    lines.push_back(SourceLine{.start = start, .content_end = content_end, .past_end = past_end});
    start = past_end;
  }
  return lines;
}

}  // namespace

std::vector<MermaidFenceBlock> scan_completed_assistant_mermaid_fences(TranscriptItem const& item)
{
  if (item.tool || item.label != "ava" || item.append_only_stream || !item.stream_id.empty())
    return {};

  auto const resolved_text = text_empty(item.text_model) ? item.text : to_plain_text(item.text_model);
  auto const text = std::string_view(resolved_text);
  auto const lines = source_lines(text);
  std::vector<MermaidFenceBlock> blocks;
  bool in_fence = false;
  bool mermaid_fence = false;
  std::size_t fence_start = 0;
  std::size_t source_start = 0;
  std::size_t block_index = 0;

  for (auto const& line : lines)
  {
    auto const content = text.substr(line.start, line.content_end - line.start);
    if (!content.starts_with("```"))
      continue;
    if (!in_fence)
    {
      in_fence = true;
      mermaid_fence = mermaid_info_token(content);
      fence_start = line.start;
      source_start = line.past_end;
      continue;
    }

    if (mermaid_fence)
    {
      auto const source_size = line.start >= source_start ? line.start - source_start : std::size_t{0};
      if (source_size <= kMaxMermaidPresentationSourceBytes)
      {
        blocks.push_back(MermaidFenceBlock{.block_index = block_index,
                                           .fence_start = fence_start,
                                           .source_start = source_start,
                                           .source_end = line.start,
                                           .fence_end = line.past_end,
                                           .source = std::string(text.substr(source_start, source_size))});
      }
      ++block_index;
    }
    in_fence = false;
    mermaid_fence = false;
  }
  return blocks;
}

MermaidTranscriptProjection::MermaidTranscriptProjection(std::uint64_t config_epoch, std::vector<MermaidAcceptedPresentation> accepted)
    : config_epoch_(config_epoch)
{
  auto const first = accepted.size() > kMaxMermaidPresentationEntries ? accepted.size() - kMaxMermaidPresentationEntries : std::size_t{0};
  std::size_t retained_bytes = 0;
  for (std::size_t index = accepted.size(); index > first; --index)
  {
    auto& candidate = accepted[index - 1];
    if (candidate.text.size() > kMaxMermaidPresentationBytes - retained_bytes)
      continue;
    retained_bytes += candidate.text.size();
    accepted_.push_back(std::move(candidate));
  }
  std::ranges::reverse(accepted_);
}

std::uint64_t MermaidTranscriptProjection::config_epoch() const noexcept
{
  return config_epoch_;
}

bool MermaidTranscriptProjection::contains_item(std::size_t item_index) const noexcept
{
  return std::ranges::any_of(accepted_, [item_index](MermaidAcceptedPresentation const& candidate) { return candidate.item_index == item_index; });
}

std::optional<std::string_view> MermaidTranscriptProjection::accepted_text(std::size_t item_index, MermaidFenceBlock const& block) const noexcept
{
  auto const entry = std::ranges::find_if(accepted_, [&](MermaidAcceptedPresentation const& candidate) {
    return candidate.config_epoch == config_epoch_ && candidate.item_index == item_index && candidate.block_index == block.block_index &&
           candidate.fence_start == block.fence_start && candidate.fence_end == block.fence_end && candidate.source == block.source;
  });
  if (entry == accepted_.end())
    return std::nullopt;
  return entry->text;
}

std::vector<MermaidAcceptedPresentation> const& MermaidTranscriptProjection::accepted() const noexcept
{
  return accepted_;
}

}  // namespace ava::tui::detail
