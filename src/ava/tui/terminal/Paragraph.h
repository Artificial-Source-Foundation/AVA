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

 public:
  Paragraph() = default;

  // Append a new TextSpan to the Paragraph.
  void append(std::unique_ptr<TextSpan>&& text_span)
  {
    text_spans_.push_back(std::move(text_span));
  }

  // Perform wrapping: return a list of TextRow's.
  std::vector<TextRow> wrap(uint32_t cell_width);

  AVA_DEBUG_PRINT_MEMBERS_ON
};

} // namespace ava::tui::terminal
