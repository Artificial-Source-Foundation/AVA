#pragma once

#include "GraphemeSpan.h"
#include "LayoutItem.h"
#include "TextSpan.h"
#include "utils/Vector.h"

#include <memory>
#include <vector>

namespace ava::tui::terminal {

// GraphemeBlock
//
// A list of one or more GraphemeSpan's of the same width (GraphemeSpan::max_columns())
// Any GraphemeBlock must exist of at least one GraphemeSpan (otherwise the width is unknown).
//
//           GraphemeBlock::front().max_columns()
//   ┊◄-----------------------------------------------►┊
//   ┏━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┯━━━━┯━━┓
//   ┃  GraphemeSpan 0                         ╎ WS │🯟🯝┃
//   ┣━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┷━━┯━┷━━╉───────┐
//   ┃  GraphemeSpan 1                            ╎ whitespace │
//   ┣━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┯━━━━━━━━┯━┷━━━━╉───────┘
//   ┃  GraphemeSpan 2                 ╎   WS   │🯟🯝🯟🯝🯟🯝┃
//   ┣━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┷━━━━━━━━┷┯━━━━━┫
//   ┃  GraphemeSpan 3                           ╎  WS ┃
//   ┗━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━┷━━━━━┛
//                   ^
//           GraphemeBlockIndex (the index into a GraphemeBlock, giving a GraphemeSpan).
//
struct GraphemeBlockCategory
{
  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};
using GraphemeBlockIndex = utils::VectorIndex<GraphemeBlockCategory>;
using GraphemeBlock = utils::Vector<GraphemeSpan, GraphemeBlockIndex>;

inline uint32_t height_of(GraphemeBlock const& block)
{
  return static_cast<uint32_t>(block.size());
}
inline columns_t width_of(GraphemeBlock const& block)
{
  return block.front().max_columns();
}

// class Paragraph
//
// A Paragraph exists of a series of TextSpan's.
//
// It can be converted into a GraphemeBlock of a given width in terminal columns, auto-wrapped to multiple lines if necessary.
//
class Paragraph : public LayoutItem
{
 private:
  std::vector<std::unique_ptr<TextSpan>> text_spans_;   // All the TextSpan's that make up this Paragraph, added in order with `append`.
  Rendition default_rendition_{ColorPair{}};            // The rendition used for TextSpan's that were created without a rendition of their own.
  Width natural_width_{0};                              // Cached sum of all the natural widths of text_spans_.

 private:
  // Construct an empty Paragraph with a default rendition that uses color pair 0 (the terminal default colors).
  Paragraph(LayoutItem::Properties const& layout_properties) : LayoutItem(layout_properties) { }

  // Construct an empty Paragraph with default rendition `default_rendition`.
  // Usually `default_rendition` equals the rendition of the (sub)window that this Paragraph will be rendered in.
  Paragraph(LayoutItem::Properties const& layout_properties, Rendition default_rendition) : LayoutItem(layout_properties), default_rendition_(default_rendition)
  {
  }

 public:
  static std::unique_ptr<Paragraph> create(LayoutItem::Properties layout_properties) { return std::unique_ptr<Paragraph>{new Paragraph{layout_properties}}; }

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

  // Wrap this Paragraph to `columns` terminal columns, returning the GraphemeSpans corresponding to that width.
  GraphemeBlock create_grapheme_block(columns_t columns) const;
  GraphemeBlock create_grapheme_block() const { return create_grapheme_block(assigned_width().columns()); }

  void write_to(BasicWindow& UNUSED_ARG(basic_window), Rendition const& UNUSED_ARG(default_rendition)) const override
  {
    // You can't call this method of a Paragraph. See HorizontalLayout::write_to for code that has the work around.
    ASSERT(false);
  }

  AVA_DEBUG_PRINT_MEMBERS_ON
};

} // namespace ava::tui::terminal
