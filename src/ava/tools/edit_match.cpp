#include "ava/tools/edit_match.h"

#include <algorithm>
#include <string>

namespace ava::tools {
namespace {

bool has_utf8_bom_prefix(std::string_view text)
{
  return text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
         static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF;
}

bool contains_lf_only_newline(std::string_view text)
{
  for (std::size_t index = 0; index < text.size(); ++index) {
    if (text[index] == '\n' && (index == 0 || text[index - 1] != '\r')) return true;
  }
  return false;
}

bool could_bom_affect_beginning_match(std::string_view content, std::string_view old_text)
{
  if (!has_utf8_bom_prefix(content) || old_text.empty() || has_utf8_bom_prefix(old_text)) return false;
  return content.substr(3).starts_with(old_text.substr(0, std::min<std::size_t>(old_text.size(), content.size() - 3)));
}

}  // namespace

TextAnalysis analyze_text(std::string_view text)
{
  TextAnalysis analysis{.has_utf8_bom = has_utf8_bom_prefix(text), .line_endings = LineEndingStyle::None};
  bool saw_lf = false;
  bool saw_crlf = false;
  bool saw_bare_cr = false;
  for (std::size_t index = 0; index < text.size(); ++index) {
    if (text[index] == '\r') {
      if (index + 1 < text.size() && text[index + 1] == '\n') {
        saw_crlf = true;
        ++index;
      } else {
        saw_bare_cr = true;
      }
    } else if (text[index] == '\n') {
      saw_lf = true;
    }
  }
  if (saw_bare_cr || (saw_lf && saw_crlf)) {
    analysis.line_endings = LineEndingStyle::Mixed;
  } else if (saw_crlf) {
    analysis.line_endings = LineEndingStyle::CRLF;
  } else if (saw_lf) {
    analysis.line_endings = LineEndingStyle::LF;
  }
  return analysis;
}

std::string to_string(LineEndingStyle style)
{
  switch (style) {
    case LineEndingStyle::None:
      return "none";
    case LineEndingStyle::LF:
      return "LF";
    case LineEndingStyle::CRLF:
      return "CRLF";
    case LineEndingStyle::Mixed:
      return "mixed";
  }
  return "unknown";
}

ava::core::Result<TextMatch> find_unique_text_match(std::string_view content, std::string_view old_text,
                                                    std::filesystem::path const& path, std::string_view missing_message,
                                                    std::string_view non_unique_message)
{
  auto const content_analysis = analyze_text(content);
  auto const old_text_analysis = analyze_text(old_text);
  auto const first = content.find(old_text);
  if (first == std::string::npos) {
    auto error = ava::core::Error(ava::core::ErrorCategory::NotFound, std::string(missing_message));
    error.with_context("path", path.string());
    error.with_context("file_line_endings", to_string(content_analysis.line_endings));
    error.with_context("old_text_line_endings", to_string(old_text_analysis.line_endings));
    if (content_analysis.line_endings == LineEndingStyle::CRLF && contains_lf_only_newline(old_text)) {
      error.with_context("hint", "file appears to use CRLF line endings; old_text contains LF-only newlines");
    }
    if (could_bom_affect_beginning_match(content, old_text)) {
      error.with_context("bom_hint",
                         "file starts with a UTF-8 BOM; include exact leading bytes if matching file start");
    }
    return std::unexpected(std::move(error));
  }

  if (content.find(old_text, first + 1) != std::string::npos) {
    auto error = ava::core::Error(ava::core::ErrorCategory::InvalidArgument, std::string(non_unique_message));
    error.with_context("path", path.string());
    return std::unexpected(std::move(error));
  }

  return TextMatch{.position = first, .size = old_text.size(), .content_analysis = content_analysis};
}

}  // namespace ava::tools
