#pragma once

#include "LayoutItem.h"
#include "TextRow.h"
#include "TextSpan.h"
#include <vector>
#include <memory>

namespace ava::tui::terminal {

class Paragraph : public LayoutItem
{
 private:
  std::vector<std::unique_ptr<TextSpan>> text_spans_;
  Rendition default_rendition_{ColorPair{}};            // The rendition used for TextSpan's that were created without a rendition of their own.
  Width natural_width_{0};                              // Cached sum of all the natural widths of text_spans_.

 private:
  // Construct an empty Paragraph with a default rendition that uses color pair 0 (the terminal default colors).
  Paragraph(LayoutItem::Properties const& layout_properties) : LayoutItem(layout_properties) { }

  // Construct an empty Paragraph with default rendition `default_rendition`.
  // Usually `default_rendition` equals the rendition of the (sub)window that this Paragraph will be rendered in.
  Paragraph(LayoutItem::Properties const& layout_properties, Rendition default_rendition) : LayoutItem(layout_properties), default_rendition_(default_rendition) { }

 public:
  static std::unique_ptr<Paragraph> create(LayoutItem::Properties layout_properties)
  {
    return std::unique_ptr<Paragraph>{new Paragraph{layout_properties}};
  }

  static std::unique_ptr<Paragraph> create(Rendition default_rendition, LayoutItem::Properties layout_properties)
  {
    return std::unique_ptr<Paragraph>{new Paragraph{layout_properties, default_rendition}};
  }

  // Append a new TextSpan to the Paragraph.
  void append(std::unique_ptr<TextSpan>&& text_span)
  {
    Width const text_span_natural_width = text_span->natural_width();
    // A greedy width is only allowed when combined with a shrink_priority of 0.
    ASSERT(!text_span_natural_width.is_greedy() || shrink_priority() == 0);
    natural_width_ += text_span_natural_width;
    text_spans_.push_back(std::move(text_span));
  }

  // Call this after all TextSpan's were added with `append`.
  void initialize_cached_natural_width() { LayoutItem::initialize_cached_natural_width(natural_width_); }

  // Accessor; the rendition used for TextSpan's without a rendition of their own.
  Rendition const& default_rendition() const { return default_rendition_; }

  // Wrap this Paragraph to `columns` terminal columns, returning TextRows for that width.
  std::vector<TextRow> wrap_to(uint32_t columns);

  AVA_DEBUG_PRINT_MEMBERS_ON
};

} // namespace ava::tui::terminal
