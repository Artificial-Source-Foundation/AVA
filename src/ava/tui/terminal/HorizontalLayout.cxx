#include "sys.h"
#include "HorizontalLayout.h"
#include <algorithm>

namespace ava::tui::terminal {
namespace {

struct Item {
  LayoutItem* ptr;
  uint32_t assigned_width;
};

void distribute(uint32_t columns, Item* items, int number_of_items)
{
  // Fast path: if there is only 1 item - then that gets it all.
  if (number_of_items == 1)
  {
    items->assigned_width = items->ptr->minimum_width().value() + columns;
    return;
  }

  // Distribute `columns` over `number_of_items` items, such that the sum of all the `assigned_width` equals the sum of their minimum widths plus columns,
  // for every Item holds that items[i].assigned_width <= items[i].ptr->natural_width(), and for all items j such that
  // items[j].assigned_width < items[j].ptr->natural_width() the expression
  // (items[j].assigned_width - items[i].ptr->minimum_width()) / items[j].ptr->shrink_weight() is more or less the same accross all j.
}

} // namespace

// The HorizontalLayout exists of one or more LayoutItem's, each of which
// has a minimum and maximum width (the natural width) and a priority.
//
// The maximum-width at priority p is the sum of the natural width's of all items of
// priority p or higher, plus the minimum width of all items with a priority less than p.
//
// The minimum-width at priority p is equal to the maximum-width at the next higher priority.
//
// For example, if we have four items with the following values, here shown
// in the order that they'd appear in `ordered_items` (where the order of items
// with the equal priority is arbitrary):
//
// ordered_items:
//   index i    priority   minimum width   natural width
//   0          5          4               13
//   1          2          8               14
//   2          2          5               12
//   3          0          3               11/unlimited                        <-- `number_of_items` is 4 in this example.
//
// Then we have:
//
// boundary:
//   index j    priority   minimum-width              maximum-width
//                                                    20                       <-- sum of minimum widths; note 20 <= columns (we force it that way).
//   0          5          20 =  3 +  5 +  8 +  4     29                       <-- corresponds with 20 <= columns < 29
//   1          2          29 =  3 +  5 +  8 + 13     42                       <-- corresponds with 29 <= columns < 42
//   2          0          42 =  3 + 12 + 14 + 13     50                       <-- corresponds with 42 <= columns, regardless of maximum-width.
//                         50/unlimited = 11/unlimited + 12 + 14 + 13
//
// The function calculates `boundary[j]` with values {20, 29, 42}, which are
// to be interpreted as the minimum-width lower bound values at which we need
// to switch to a lower priority.
//
void HorizontalLayout::set_width(uint32_t columns)
{
  static constexpr std::size_t max_items = 16;
  int const number_of_items = layout_items_.size();
  // Increase max_items if required.
  ASSERT(number_of_items <= max_items);
  if (number_of_items == 0)
    return;
  // Sort the items on the stack by priority from high to low.
  std::array<Item, max_items> ordered_items;
  std::size_t item_count = 0;
  uint32_t sum_of_widths = 0;
  for (std::unique_ptr<LayoutItem> const& layout_item : layout_items_)
  {
    sum_of_widths += layout_item->minimum_width().value();
    ordered_items[item_count++].ptr = layout_item.get();
  }
  std::sort(
    ordered_items.begin(),
    ordered_items.begin() + item_count,
    [](Item const& lhs, Item const& rhs){
      return lhs.ptr->shrink_priority() > rhs.ptr->shrink_priority();
    });
  // `ordered_items` now contains the first table with index i.
  // `sum_of_widths` is now 20.

  // If columns is less than the absolute minimum then we just can't do that; render the absolute minimum and hope for the best.
  columns = std::max(columns, sum_of_widths);
  int columns_j = 0;                                                    // The range that `columns` falls into.
  uint32_t columns_priority;                                            // The priority corresponding with that range.

  // Run over all ordered items and fill the `boundary` table.
  std::array<uint32_t, LayoutItem::max_priority + 1> boundary;
  int j = 0;
  uint32_t prev_priority = LayoutItem::max_priority + 1;             // Something larger than any priority.
  for (int i = 0; i < number_of_items; ++i)
  {
    if (ordered_items[i].ptr->shrink_priority() < prev_priority)
    {
      prev_priority = ordered_items[i].ptr->shrink_priority();

      // All boundaries are expected to represent actual terminal columns.
      ASSERT(sum_of_widths < 1000000U);

      if (columns >= sum_of_widths)
      {
        columns_j = j;
        columns_priority = prev_priority;
      }

      boundary[j++] = sum_of_widths;                                    // Assign 20 the first time, then 29 while prev_priority == 5, 42 while prev_priority == 2.

      // Once we find an item with priority 0 we can exit this loop, because we'll never find a lower
      // priority and thus will never get here again (to assign a new value to `boundary`.
      // Moreover, we can't calculate the difference between natural_width() and minimum_width() anymore because the natural width might be unlimited.
      if (prev_priority == 0)
        break;
    }
    sum_of_widths += (ordered_items[i].ptr->natural_width() - ordered_items[i].ptr->minimum_width()).value();
    // That runs `sum_of_widths` over the values:
    //   i      sum_of_widths
    //   0      20 + (13 - 4) = 29
    //   1      29 + (14 - 8) = 35
    //   2      35 + (12 - 5) = 42
    //   The loop did break before we get here:
    //   3      42 + (11/unlimited - 3) = 50/unlimited
  }

  // The number of columns that `columns` is larger than the minimum-width of the columns_j range.
  uint32_t const columns_delta = columns - boundary[columns_j];

  // Set assigned_width_ on all items that have minimum or maximum width,
  // and store the items that have a width somewhere in between those values.
  int first_flex_item = -1;
  int last_flex_item = -1;
  for (int i = 0; i < number_of_items; ++i)
  {
    Item& item = ordered_items[i];
    uint32_t const priority = item.ptr->shrink_priority();
    if (priority < columns_priority)
      item.assigned_width = item.ptr->minimum_width().value();
    else if (priority > columns_priority)
      item.assigned_width = item.ptr->natural_width().value();
    else
    {
      if (first_flex_item == -1)
        first_flex_item = i;
      last_flex_item = i;
    }
  }
  // There is always an item with a priority equal to columns_priority.
  ASSERT(last_flex_item != -1);

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

  distribute(columns_delta, &ordered_items[first_flex_item], last_flex_item - first_flex_item + 1);
  for (int i = 0; i < number_of_items; ++i)
    ordered_items[i].ptr->assigned_width_ = ordered_items[i].assigned_width;
}

} // namespace ava::tui::terminal
