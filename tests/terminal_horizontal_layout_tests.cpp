#include "sys.h"
#include "terminal/HorizontalLayout.h"
#include "terminal/Paragraph.h"
#include "terminal/Spacer.h"
#include "terminal/TextSpan.h"
#include "tests/support/test_harness.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace terminal = ava::tui::terminal;

namespace {

// Verify the assigned width of `item` against `expected_width`, identifying failures with `name`.
void expect_assigned_width(terminal::LayoutItem const& item, uint32_t expected_width, std::string_view name)
{
  uint32_t const actual_width = item.assigned_width().value();
  expect(actual_width == expected_width,
         std::string{name} + " must be assigned " + std::to_string(expected_width) + " columns, got " + std::to_string(actual_width));
}

// Verify priority negotiation among an exit label, greedy spacer, and right-aligned shortcut paragraph.
void test_status_line_layout()
{
  auto exit_text = terminal::TextSpan::create(u8"Exit the app", {.priority = 1});
  terminal::TextSpan const* exit_text_ptr = exit_text.get();
  auto spacer = terminal::Spacer::create();
  terminal::Spacer const* spacer_ptr = spacer.get();
  auto shortcuts = terminal::Paragraph::create({.priority = 2, .alignment = terminal::HorizontalAlignment::right});
  terminal::Paragraph const* shortcuts_ptr = shortcuts.get();
  shortcuts->append(terminal::TextSpan::create(u8"ctrl+c, ctrl+d, ctrl+x q"));
  shortcuts->initialize_cached_natural_width();

  terminal::HorizontalLayout horizontal_layout;
  horizontal_layout.append(std::move(exit_text));
  horizontal_layout.append(std::move(spacer));
  horizontal_layout.append(std::move(shortcuts));
  horizontal_layout.set_width(17);

  expect_assigned_width(*exit_text_ptr, 3, "exit label");
  expect_assigned_width(*spacer_ptr, 1, "status-line spacer");
  expect_assigned_width(*shortcuts_ptr, 13, "shortcut paragraph");
}

// Verify that mixed priorities reach their natural or intermediate widths in the documented boundary example.
void test_priority_boundary_layout()
{
  auto item0 = terminal::TextSpan::create(u8"1234567890123", {.priority = 5, .minimum_width = 4});
  terminal::TextSpan const* item0_ptr = item0.get();
  auto item1 = terminal::TextSpan::create(u8"12345678901234", {.priority = 2, .minimum_width = 8});
  terminal::TextSpan const* item1_ptr = item1.get();
  auto item2 = terminal::TextSpan::create(u8"123456789012", {.priority = 2, .minimum_width = 5});
  terminal::TextSpan const* item2_ptr = item2.get();
  auto item3 = terminal::Spacer::create({.minimum_width = 3});
  terminal::Spacer const* item3_ptr = item3.get();

  terminal::HorizontalLayout horizontal_layout;
  horizontal_layout.append(std::move(item3));
  horizontal_layout.append(std::move(item0));
  horizontal_layout.append(std::move(item2));
  horizontal_layout.append(std::move(item1));
  horizontal_layout.set_width(32);

  expect_assigned_width(*item3_ptr, 3, "boundary item 3");
  expect_assigned_width(*item0_ptr, 13, "boundary item 0");
  expect_assigned_width(*item2_ptr, 7, "boundary item 2");
  expect_assigned_width(*item1_ptr, 9, "boundary item 1");
}

// Verify weighted distribution when one item reaches its natural width and the others remain flexible.
void test_weighted_layout()
{
  auto item_a = terminal::TextSpan::create(u8"12345678901234567890", {.weight = 1.0f, .minimum_width = 5});
  terminal::TextSpan const* item_a_ptr = item_a.get();
  auto item_b = terminal::TextSpan::create(u8"1234567890123456789012345678901", {.weight = 1.5f, .minimum_width = 7});
  terminal::TextSpan const* item_b_ptr = item_b.get();
  auto item_c = terminal::TextSpan::create(u8"12345678901234567", {.weight = 2.0f, .minimum_width = 11});
  terminal::TextSpan const* item_c_ptr = item_c.get();

  terminal::HorizontalLayout horizontal_layout;
  horizontal_layout.append(std::move(item_a));
  horizontal_layout.append(std::move(item_b));
  horizontal_layout.append(std::move(item_c));
  horizontal_layout.set_width(43);

  expect_assigned_width(*item_a_ptr, 11, "weighted item A");
  expect_assigned_width(*item_b_ptr, 15, "weighted item B");
  expect_assigned_width(*item_c_ptr, 17, "weighted item C");
}

} // namespace

// Run deterministic HorizontalLayout width-negotiation coverage without initializing ncurses or debug output.
void run_terminal_horizontal_layout_tests()
{
  test_status_line_layout();
  test_priority_boundary_layout();
  test_weighted_layout();
}
