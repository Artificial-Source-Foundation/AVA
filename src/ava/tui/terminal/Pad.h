#pragma once

#include "Paragraph.h"
#include <list>

namespace ava::tui::terminal {

class Pad
{
 private:
  std::list<Paragraph> paragraphs_;     // A list of Paragraphs.

 public:
  Pad() = default;

  void append(Paragraph&& paragraph)
  {
    paragraphs_.push_back(std::move(paragraph));
  }

  AVA_DEBUG_PRINT_MEMBERS_ON
};

} // namespace ava::tui::terminal
