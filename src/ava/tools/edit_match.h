#pragma once

#include "ava/core/result.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

namespace ava::tools {

enum class LineEndingStyle { None, LF, CRLF, Mixed };

struct TextAnalysis {
  bool has_utf8_bom = false;
  LineEndingStyle line_endings = LineEndingStyle::None;
};

struct TextMatch {
  std::size_t position = 0;
  std::size_t size = 0;
  TextAnalysis content_analysis;
};

[[nodiscard]] TextAnalysis analyze_text(std::string_view text);
[[nodiscard]] std::string to_string(LineEndingStyle style);
[[nodiscard]] ava::core::Result<TextMatch> find_unique_text_match(std::string_view content, std::string_view old_text,
                                                                  std::filesystem::path const& path,
                                                                  std::string_view missing_message,
                                                                  std::string_view non_unique_message);

}  // namespace ava::tools
