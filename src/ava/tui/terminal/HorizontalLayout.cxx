#include "sys.h"
#include "HorizontalLayout.h"
#include "Paragraph.h"
#include "utils/macros.h"
#include <algorithm>
#include <span>
#include <numeric>
#include <cmath>

namespace ava::tui::terminal {
namespace {

static constexpr std::size_t max_items = 16;

struct Item {
  LayoutItem* ptr;
  uint32_t assigned_width;
};

void distribute(uint32_t columns, std::span<Item> items)
{
  // Fast path: if there is only 1 item - then that gets it all.
  if (items.size() == 1)
  {
    // Assign all columns to the first (and only) item.
    items.front().assigned_width = items.front().ptr->minimum_width().value() + columns;
    return;
  }

  // Distribute `columns` over `number_of_items` items, such that afterwards the sum of all the
  // `assigned_width` members equals the sum of their minimum widths plus `columns`;
  //
  // that is: each of the first `number_of_items` `Item`s is assigned:
  //
  //   items[k].assigned_width = items[k].ptr->minimum_width().value() + columns_part(k)
  //
  // where the sum of columns_part(k) over all k equals `columns`.
  //
  // Also, for each of the first `number_of_items` `Item`s it holds afterwards, that:
  //
  //   items[k].assigned_width <= items[k].ptr->natural_width(),
  //
  // and for all items l such that items[l].assigned_width < items[l].ptr->natural_width(),
  // the expression `columns_part(l) / items[j].ptr->shrink_weight()` is more or less the same
  // accross all l.

  // We can convert this problem to the following:
  //
  // Consider `N = items.size()` tubes with a depth of 1 and a width of w(k), where 0 <= k < N.
  // That is, if tube `k` is filled with oil such that its level rises with `Δh` then `Δh·w(k)` of oil was added.
  //
  // Assume the tubes are connected at the bottom, the connections already full of oil.
  // Initially the level in all tubes is the same, the height of which we call 0.
  // Each tube has `h(k)` more room above this level. The top of each cylinder is closed.
  // The empty space above the initial level, with volume `h(k)·w(k)` is vacuum.
  // The tallest tube is assumed to be arbitrary long.
  //
  // We fill the tubes with a `column` amount of extra oil. The level in all tubes stays
  // the same although any given tube can not be filled any higher than `h(k)`.

  // There is always at least one item, and we already handled the case where there is only one.
  ASSERT(items.size() > 1);

  struct Tube           // Tube k
  {
    uint32_t delta;     // natural_width - minimum_width : the maximum number of columns on top of minimum_width.
    float weight;       // w(k), or zero when the Tube is full.

    // Return the height of the tube.
    float height() const { return delta / weight; }
    bool is_full() const { return weight == 0.f; }

    void set_full() { weight = 0.f; }
  };

  std::array<Tube, max_items> tubes;
  float active_weight = 0.f;

  // Initialize each tube with the correct delta and weight.
  for (int k = 0; k < items.size(); ++k)
  {
    LayoutItem const* layout_item = items[k].ptr;
    tubes[k] = Tube{
      .delta = layout_item->natural_width().is_unlimited() ? Width::unlimited
                                                           : (layout_item->natural_width() - layout_item->minimum_width()).value(),
      .weight = layout_item->shrink_weight()
    };

    active_weight += layout_item->shrink_weight();
  }

  // Sort the k values for increasing height.
  std::array<int, max_items> ok;        // Ordered k values.
  std::iota(ok.begin(), ok.begin() + items.size(), 0);

  std::sort(
    ok.begin(),
    ok.begin() + items.size(),
    [&tubes](int lhs, int rhs)
    {
      return tubes[lhs].height() < tubes[rhs].height();
    });
  // Now tubes[ok[i]] is ordered by height for increasing 0 <= i < items.size().

  float current_level = 0.f;            // The largest height that was handed out so far (also: the current oil level).
  float final_level;                    // The final largest height added to any tube.
  int first = 0;                        // The first of the tubes that is still going to be filled (any tube below `first` is already full).
  float remaining_volume = columns;     // The remaining number of columns that we still need to distribute (also: the remaining volume of
                                        // oil that needs to be transfered into the tubes).
  for (;;)
  {
    // The level at which the next tube will run full.
    float const next_level = tubes[ok[first]].height();
    // The additional amount of oil required to reach that level.
    // Note that `active_weight` corresponds to the cross-section of the tubes that are not yet full.
    float const required_volume = (next_level - current_level) * active_weight;

    if (remaining_volume <= required_volume)
    {
      // We don't reach `next_level`; calculate the final level and continue.
      final_level = current_level + remaining_volume / active_weight;
      break;
    }

    remaining_volume -= required_volume;
    current_level = next_level;

    // Remove all tubes that become full at this level.
    do
    {
      // Adjust the cross-section of the not-full tubes.
      active_weight -= tubes[ok[first]].weight;
      tubes[ok[first]].set_full();
      ++first;

      // At least one sufficiently large tube must remain, otherwise there is no way to know how/where to distribute the excess columns.
      // If this asserts then the Properties of one or more LayoutItem's needs to be changed so the algorithm can know how to distribute
      // the required columns.
      ASSERT(first != items.size());
    }
    while (tubes[ok[first]].height() <= current_level * 1.0001f);
  }

  // Run over all items again and assign the natural width to all items that were stretched to their maximum width (the 'tube' was completely filled).
  uint32_t remaining_columns = columns;
  for (int k = 0; k < items.size(); ++k)
  {
    LayoutItem const* layout_item = items[k].ptr;
    if (tubes[k].is_full())
    {
      items[k].assigned_width = layout_item->natural_width().value();
      remaining_columns -= tubes[k].delta;
    }
    else
    {
      // For now, assign the minimum width (level 0) to non-full tubes.
      items[k].assigned_width = layout_item->minimum_width().value();
    }
  }

  // The situation is now as follows:
  //                                            │    │
  //                                   │     │  │    │
  //                       full  full  │⎻⎻⎻⎻⎻│┄┄│⎻⎻⎻⎻│┄ ⟵ final_level
  //                        ▼     ▼    │▒▒▒▒ │  │▒▒▒▒│
  //                                   │▒▒▒▒▒│  │▒▒▒▒│
  //   current_level ──➤ ┄┄┄┄┄┄┄┄│⎻⎻│┄┄│▒▒▒▒▒│┄┄│▒▒▒▒│┄
  //                    ˰ │⎻⎻⎻│  │▓▓│  │▒▒▒▒▒│  │▒▒▒▒│  ▓ = already filled (natural_width - minimum_width for each item).
  //                    │ │▓▓▓│  │▓▓│  │▒▒▒▒▒│  │▒▒▒▒│
  // tubes[k].height()──│ │▓▓▓│  │▓▓│  │▒▒▒▒▒│  │▒▒▒▒│  ▒ = remaining_columns (62)
  //                    │ │▓▓▓│  │▓▓│  │▒▒▒▒▒│  │▒▒▒▒│
  //                    ˅ └───┘  └──┘  └─────┘  └────┘┄ ⟵ level of minimum_width of each item (level 0).
  //                weight: 3      2      5        4
  //                        ^      ^   ╰──────┬──────╯
  //                      set to zero  remaining items

  // Distribute the remaining_columns over the remaining items.

  // Reorder the items by the fractional part of the extra columns each should get, from large to small.
  std::iota(ok.begin(), ok.begin() + items.size(), 0);
  std::sort(
    ok.begin(),
    ok.begin() + items.size(),
    [&tubes, final_level](int lhs, int rhs)
    {
      float lhs_desired_extra_columns = final_level * tubes[lhs].weight;
      float rhs_desired_extra_columns = final_level * tubes[rhs].weight;
      float lhs_fractional_part = lhs_desired_extra_columns - std::floor(lhs_desired_extra_columns);
      float rhs_fractional_part = rhs_desired_extra_columns - std::floor(rhs_desired_extra_columns);
      return lhs_fractional_part > rhs_fractional_part;
    });

  for (int i = 0; remaining_columns > 0 && i < items.size(); ++i)
  {
    int k = ok[i];
    if (tubes[k].is_full())
      continue;
    float desired_extra_columns = final_level * tubes[k].weight;
    uint32_t extra_columns = std::floor(desired_extra_columns);
    items[k].assigned_width += extra_columns;
    remaining_columns -= extra_columns;
  }

  // Assign the last remaining columns, one per item, starting with the ones that have the largest fractional part.
  for (int i = 0; remaining_columns > 0 && AI_LIKELY(i < items.size()); ++i)
  {
    int k = ok[i];
    if (AI_UNLIKELY(tubes[k].is_full()))        // This should never happen because full tubes have their weight set to 0
      continue;                                 // and are therefore sorted last: all remaining columns should already have been distributed.
    items[k].assigned_width += 1;
    --remaining_columns;
  }

  // Paranoia check: every requested column must have been distributed by this point.
  ASSERT(remaining_columns == 0);
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
  size_t number_of_flex_items = 0;
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
      ++number_of_flex_items;
    }
  }
  // There is always an item with a priority equal to columns_priority.
  ASSERT(number_of_flex_items > 0);

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

  distribute(columns_delta, {ordered_items.begin() + first_flex_item, number_of_flex_items});

  // Assign `assigned_width_` of each item.
  for (int i = 0; i < number_of_items; ++i)
    ordered_items[i].ptr->assigned_width_ = ordered_items[i].assigned_width;
}

void HorizontalLayout::write_to(BasicWindow& basic_window, Rendition const& default_rendition) const
{
  int const number_of_items = layout_items_.size();

  // You can't write an empty HorizontalLayout. Use the `append` member function to fill it.
  ASSERT(number_of_items > 0);

  std::vector<std::vector<GraphemeSpan>> paragraph_grapheme_spans;
  std::size_t max_paragraph_rows = 0;

  for (int i = 0; i < number_of_items; ++i)
  {
    LayoutItem const* layout_item = layout_items_[i].get();
    // Paragraph's need special treatment.
    if (Paragraph const* paragraph = dynamic_cast<Paragraph const*>(layout_item))
    {
      paragraph_grapheme_spans.emplace_back(paragraph->create_grapheme_spans());
      max_paragraph_rows = std::max(max_paragraph_rows, paragraph_grapheme_spans.back().size());
      paragraph_grapheme_spans.back().front().write_to(basic_window, default_rendition);
    }
    // The rest is assumed to exist of only a single grapheme run.
    else
      layout_item->write_to(basic_window, default_rendition);
  }
}

} // namespace ava::tui::terminal
