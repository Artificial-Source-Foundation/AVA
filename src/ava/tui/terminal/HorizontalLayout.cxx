#include "sys.h"
#include "HorizontalLayout.h"

namespace ava::tui::terminal {
namespace {

std::vector<uint32_t> distribute(uint32_t columns, std::vector<LayoutItem*> const& items)
{
  std::vector<uint32_t> distribution;

  // Fast path: if there is only 1 item - then that gets it all.
  if (items.size() == 1)
    distribution.push_back(columns);
  else
  {
    // Distribute columns over N items, where N == items.size(),
    // such that the sum of all N elements of distribution equals columns.
    // For every element holds distribution[i] <= items[i]->natural_width() and
    // for all elements j such that distribution[j] < items[j]->natural_width()
    // the expression distribution[j] / items[j]->shrink_weight() is more or less
    // the same accross all j.
  }

  return distribution;
}

} // namespace

// The HorizontalLayout exists of one or more LayoutItem's, each of which
// has a minimum and maximum width (the natural width) and a priority.
//
// The maximum-width at priority p is the sum of the natural width's of all items of
// priority p or higher, plus the minimum width of all items with a priority less than p.
//
// The minimum-width at priority p is equal to the maximum-width at priority p+1.
//
// For example, if we have three items with the following values:
//
//   priority   minimum width   natural width   delta
//   0          3               11              11-3 = 8
//   1          5               12              12-5 = 7
//   2          8               14              14-8 = 6        <-- highest_priority is 2 in this example.
//
// Then we have:
//
//   priority   minimum-width   maximum-width
//   0          14+12+ 3 = 29   14+12+11 = 37
//   1          14+ 5+ 3 = 22   14+12+ 3 = 29   <-- columns_priority will be 1 if 22 <= columns < 29
//   2           8+ 5+ 3 = 16   14+ 5+ 3 = 22
//   3                           8+ 5+ 3 = 16   <-- pseudo priority with as maximum-width the sum of all minimum width's.
//
void HorizontalLayout::set_width(uint32_t columns)
{
  // Run over all items, calculate the delta's and determine the highest shrink_priority used.
  std::array<Width, LayoutItem::max_priority + 1> delta;
  uint32_t highest_priority = 0;
  Width sum_of_minimum_widths{0U};
  for (std::unique_ptr<LayoutItem> const& layout_item : layout_items_)
  {
    uint32_t const priority = layout_item->shrink_priority();
    highest_priority = std::max(highest_priority, priority);
    Width item_delta = layout_item->natural_width() - layout_item->minimum_width();
    if (delta[priority].is_unknown())
      delta[priority] = item_delta;
    else
      delta[priority] += item_delta;
    sum_of_minimum_widths += layout_item->minimum_width();
  }

  // If columns is less than the absolute minimum then we just can't do that; render the absolute minimum and hope for the best.
  columns = std::max(columns, sum_of_minimum_widths.value());
  int columns_priority = highest_priority;                              // The priority that `columns` falls into.

  // Run over all used priorities and fill the `maximum-width` table.
  std::array<Width, LayoutItem::max_priority + 2> maximum_width;
  maximum_width[highest_priority + 1] = sum_of_minimum_widths;          // 8+ 5+ 3 = 16
  for (uint32_t p = highest_priority + 1; p > 0; --p)                   // p runs from 3 to 1.
  {
    maximum_width[p - 1] = maximum_width[p] + delta[p - 1];             // 16 + 6 = 22 (first time)
    // Only maximum_width[0] may become `unlimited`, the rest is expected to represent actual terminal columns.
    ASSERT((p == 1 && maximum_width[0].is_unlimited()) || maximum_width[p - 1].value() < 1000000U);
    if (columns >= maximum_width[p].value())
      columns_priority = p - 1;
  }

  // The number of columns that `columns` is larger than the minimum-width of the columns_priority range.
  uint32_t const columns_delta = (columns - maximum_width[columns_priority + 1]).value();

  std::vector<LayoutItem*> flex_items;
  for (std::unique_ptr<LayoutItem> const& layout_item : layout_items_)
  {
    uint32_t const priority = layout_item->shrink_priority();
    if (priority < columns_priority)
      layout_item->assigned_width_ = layout_item->minimum_width();
    else if (priority > columns_priority)
      layout_item->assigned_width_ = layout_item->natural_width();
    else
      flex_items.push_back(layout_item.get());
  }

  // Distribute the surplus columns `columns_delta` over the items with priority `columns_priority`, giving
  // the item with a larger weight proportionally more.
  // For example, say we have three items with the same priority and minimum/maximum widths:
  //
  //                      A         B         C
  //   minimum width |    5         7        11
  //   natural width |   20        31        17
  //          weight |  1.0       1.5       2.0
  //
  // If columns_delta is 0 then each must be set to its minimum: A=5, B=7 and C=11.
  // If columns_delta is 45 then each must be set to its maximum: A=5+15, B=7+24 and C=11+6 - where 15+24+6=45.
  //
  // If columns_delta is -say- 20 then we must divide 20 over A, B and C in the ratio 1.0, 1.5 and 2.0 until
  // one of them runs full, which will be C. Thus C gets the full 17-11=6, and A and B respectively get 3 and 4.5.
  // We then distributed 6 + 3 + 4.5 = 13.5, still 6.5 to go. Those will be distributed over the remaining items,
  // A and B until one runs full, would first be A, but that already doesn't happen. A gets 2.6 and B gets 3.9
  // (note that 2.6 * 1.5 = 3.9 and 2.6 + 3.9 = 6.5). In the end we gave A 3+2.6 = 5.6, B 4.5+3.9=8.4 and C 6.
  // We can't give fractions of columns, so after rounding off we give A 6 and B 8. Note that 6+8+6=20.

  auto assigned_columns = distribute(columns_delta, flex_items);
  for (int i = 0; i != flex_items.size(); ++i)
    flex_items[i]->assigned_width_ = flex_items[i]->minimum_width().value() + assigned_columns[i];
}

} // namespace ava::tui::terminal
