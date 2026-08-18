#pragma once

#include "HorizontalAlignment.h"

#include <limits>
#include "debug.h"                      // ASSERT

namespace ava::tui::terminal {

// class LayoutItem
//
// The base class for items arranged side by side in a HorizontalLayout.
// The layout gives every item the same height, equal to the greatest
// height required by any item at its negotiated width.
//
// Each item provides the sizing information used by HorizontalLayout
// to negotiate widths. If an item is allocated more than its natural
// width, its content is positioned within the surplus space according
// to its horizontal alignment.
//
class LayoutItem
{
 public:
  static constexpr float default_shrink_weight = 1.0f;
  static constexpr int default_minimum_width = 3;
  static constexpr int greedy = std::numeric_limits<int>::max();

  struct Properties
  {
    // Used for width negotiation.

    // The priority at which this item participates in shrinking.
    // Items with lower priorities are shrunk to their minimum widths before items with higher priorities begin shrinking.
    uint32_t priority = 0;

    // The relative rate at which this item shrinks alongside other items having the same shrink priority.
    float weight = default_shrink_weight;

    // The smallest width that may be allocated to this item, measured in terminal columns.
    int minimum_width = default_minimum_width;

    // Used for alignment.

    // The alignment of the content when the allocated width exceeds its natural width.
    HorizontalAlignment alignment = HorizontalAlignment::left;

    AVA_DEBUG_PRINT_MEMBERS_ON
  };

 private:
  Properties properties_;

  // Returns the natural width of this item, measured in terminal columns.
  // Allocating more width will result in white space and require horizontal alignment.
  //
  // The default implementation gives the item an unbounded natural width, making it greedy. Such an item must have shrink priority zero.
  // Derived classes with a finite natural width must override this function.
  virtual int do_natural_width() const
  {
    // Either implement `do_natural_width` in the derived class, returning a finite value, or use a priority of 0.
    ASSERT(properties_.priority == 0);
    return greedy;
  }

 public:
  // Construct a LayoutItem from a Properties aggregate with defaults.
  LayoutItem(Properties properties) : properties_(properties) { }

  // Move constructor.
  LayoutItem(LayoutItem&& layout_item) : properties_(std::move(layout_item.properties_)) { }

  // Destructor.
  virtual ~LayoutItem() = default;

  // Accessors.
  int shrink_priority() const { return properties_.priority; }
  float shrink_weight() const { return properties_.weight; }
  int minimum_width() const { return properties_.minimum_width; }
  int natural_width() const { return do_natural_width(); }
  HorizontalAlignment horizontal_alignment() const { return properties_.alignment; }

  AVA_DEBUG_PRINT_MEMBERS_ON
};

} // namespace ava::tui::terminal
