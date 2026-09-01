#pragma once

#include "Box.h"
#include "GraphemeBlock.h"
#include "HorizontalAlignment.h"

#include <cstdint>
#include <limits>
#include <memory>
#include "debug.h"                      // ASSERT

namespace ava::tui::terminal {

// A finite count of terminal columns. These are actual columns, not 'unknown' or 'unlimited'.
using columns_t = uint32_t;

// Forward declarations.
class Width;
Width operator-(Width w1, Width w2);

class Width
{
  static constexpr uint32_t unknown = std::numeric_limits<uint32_t>::max();

 public:
  static constexpr uint32_t unlimited = std::numeric_limits<int>::max();

 private:
  uint32_t columns_;    // Either `unknown`, `unlimited` or a finite number of columns.

 public:
  // Construct an unknown Width.
  constexpr Width() : columns_(unknown) { }

  // Construct a Width from an integer value. The value can be `unlimited/greedy`.
  constexpr Width(uint32_t columns) : columns_(columns)
  {
    // Do not construct a Width with an unknown value using this constructor.
    ASSERT(columns_ != unknown);
  }

  Width& operator+=(Width w);

  columns_t columns() const
  {
    // Should only use `value` for known, finite values.
    ASSERT(!is_unknown() && !is_unlimited());
    return columns_;
  }

  friend Width operator+(Width w1, Width w2);
  friend Width operator-(Width w1, Width w2);
  friend bool operator!=(Width w1, Width w2) { return w1.columns_ != w2.columns_; }
  friend bool operator<(Width w1, Width w2);
  bool is_greedy() const { return columns_ == unlimited; }
  bool is_unlimited() const { return columns_ == unlimited; }
  bool is_unknown() const { return columns_ == unknown; }

#ifdef CWDEBUG
  void print_on(std::ostream& os) const;
#endif

  // We have a custom print_on.
  AVA_DEBUG_PRINT_MEMBERS_OPT_OUT
};

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
  static constexpr Width default_minimum_width = 3;
  static constexpr Width greedy{Width::unlimited};
  static constexpr Width unknown{};
  static constexpr uint32_t max_priority = 8;

  struct Properties
  {
    // Used for width negotiation.

    // The priority at which this item participates in shrinking.
    // Items with lower priorities are shrunk to their minimum widths before items with higher priorities begin shrinking.
    uint32_t priority = 0;

    // The relative rate at which this item shrinks alongside other items having the same shrink priority.
    float weight = default_shrink_weight;

    // The smallest width that may be allocated to this item, measured in terminal columns.
    Width minimum_width = default_minimum_width;

    // Used for alignment.

    // The alignment of the content when the allocated width exceeds its natural width.
    HorizontalAlignment alignment = HorizontalAlignment::left;

    AVA_DEBUG_PRINT_MEMBERS_ON
  };

 private:
  Properties properties_;
  Width cached_natural_width_;          // Cached value of a call to `obtain_natural_width` by the most-derived class.

  friend class HorizontalLayout;
  Width assigned_width_{unknown};       // The assigned width in terminal columns.

 public:
  // Construct a LayoutItem from a Properties aggregate with defaults.
  LayoutItem(Properties properties) : properties_(properties)
  {
    // A priority should be some small integral value. If really necessary, increase max_priority
    // but keep in mind that we need to create a std::array on the stack on that size.
    ASSERT(properties_.priority < max_priority);
    // A weight is not allowed to be less or equal zero; you must be able to divide by it.
    // In fact it is stronly encouraged to only use weights greater than or equal 1.
    ASSERT(properties_.weight > 0.1f);
  }

  // Move constructor.
  LayoutItem(LayoutItem&& layout_item) : properties_(std::move(layout_item.properties_)) { }

  // Finalize initialization. This must be called from the most-derived class.
  void initialize_cached_natural_width(Width natural_width)
  {
    // Don't initialize with an unknown width.
    ASSERT(!natural_width.is_unknown());
    cached_natural_width_ = natural_width;
  }

  // Destructor.
  virtual ~LayoutItem() = default;

  // Accessors.

  uint32_t shrink_priority() const { return properties_.priority; }
  float shrink_weight() const { return properties_.weight; }

  Width minimum_width() const
  {
    // Clamp minimum_width to the natural width. This is used for example to set a fixed width by passing a Properties::minimum_width of `Width::greedy`.
    return cached_natural_width_ < properties_.minimum_width ? cached_natural_width_ : properties_.minimum_width;
  }

  Width natural_width() const
  {
    // Call initialize_cached_natural_width() after the most-derived class is fully initialized.
    ASSERT(!cached_natural_width_.is_unknown());
    return cached_natural_width_;
  }

  HorizontalAlignment horizontal_alignment() const { return properties_.alignment; }

  Width assigned_width() const
  {
    // Call `HorizontalLayout::set_width` to calculate and assign the width of its children.
    ASSERT(assigned_width_ != unknown);
    return assigned_width_;
  }

  // Convert this item to a GraphemeBlock at the width assigned by HorizontalLayout::set_width.
  //
  // The returned block always contains at least one row and retains views into this item where
  // applicable. The item must therefore outlive the block and any surface containing it.
  virtual GraphemeBlock create_grapheme_block() const = 0;

  AVA_DEBUG_PRINT_MEMBERS_ON
};

} // namespace ava::tui::terminal
