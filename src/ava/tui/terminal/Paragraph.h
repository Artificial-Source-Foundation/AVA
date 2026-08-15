#pragma once

#include "TextRow.h"
#include "TextSpan.h"
#include <vector>
#include <memory>

namespace ava::tui::terminal {

class Paragraph
{
 private:
  std::vector<std::unique_ptr<TextSpan>> text_spans_;
  Rendition default_rendition_{ColorPair{}};    // The rendition used for TextSpan's that were created without a rendition of their own.

 public:
  // Construct an empty Paragraph with a default rendition that uses color pair 0 (the terminal default colors).
  Paragraph() = default;

  // Construct an empty Paragraph with default rendition `default_rendition`.
  // Usually `default_rendition` equals the rendition of the (sub)window that this Paragraph will be rendered in.
  explicit Paragraph(Rendition default_rendition) : default_rendition_(default_rendition) { }

  // Append a new TextSpan to the Paragraph.
  void append(std::unique_ptr<TextSpan>&& text_span)
  {
    text_spans_.push_back(std::move(text_span));
  }

  // Accessor; the rendition used for TextSpan's without a rendition of their own.
  Rendition const& default_rendition() const { return default_rendition_; }

  // Perform wrapping: return a list of TextRow's.
  std::vector<TextRow> wrap(uint32_t cell_width);

  AVA_DEBUG_PRINT_MEMBERS_ON
};

} // namespace ava::tui::terminal
