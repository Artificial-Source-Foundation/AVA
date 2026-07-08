#include "sys.h"
#include "tests/support/test_harness.h"

#include "ava/debug/debug_ostream_operators.h"
#include "utils/to_string.h"

#include <boost/smart_ptr/intrusive_ref_counter.hpp>
#include <boost/intrusive_ptr.hpp>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include "debug.h"

// A type in namespace ava::session that owns a print_on method, used to exercise
// printing of an owning pointer (boost::intrusive_ptr) and an optional of one.
namespace ava::session {
struct Foo : boost::intrusive_ref_counter<Foo, boost::thread_unsafe_counter> {
  void print_on(std::ostream& os) const { os << "{Foo}"; }
};
} // namespace ava::session

namespace {

// An enum with an ADL-findable to_string; after LIBCWD_USING_OSTREAM_PRELUDE it
// should render as that string.
namespace test_adl {
enum class Color { Red, Green, Blue };
std::string to_string(Color color)
{
  switch (color)
  {
    // Use uppercase to distinguish this function from utils::to_string.
    case Color::Red: return "RED";
    case Color::Green: return "GREEN";
    case Color::Blue: return "BLUE";
  }
  return "Unknown";
}
} // namespace test_adl

// An enum WITHOUT a to_string. This is the case the prelude still needs to grow
// support for (scoped enums cannot fall back to their underlying integer).
enum class Plain { Alpha, Beta, Gamma };

// Helper that builds the debug rendering of every supported value kind in a
// single stringstream, using LIBCWD_USING_OSTREAM_PRELUDE exactly the way a
// print_on/print_members body would.
std::string render_debug_values()
{
  std::stringstream ss;

  std::string const string_value = "hello";
  bool const bool_value = true;
  int const int_value = 42;
  test_adl::Color const color_value = test_adl::Color::Green;
  Plain const plain_value = Plain::Beta;
  std::mutex mutex_value;
  std::optional<std::string> const optional_string_value = std::string("present");
  std::optional<std::string> const optional_string_empty;
  ava::session::Foo const foo;
  boost::intrusive_ptr<ava::session::Foo> const foo_ptr(new ava::session::Foo);
  std::optional<boost::intrusive_ptr<ava::session::Foo>> const optional_foo(foo_ptr);
  std::optional<boost::intrusive_ptr<ava::session::Foo>> const optional_foo_empty;

  {
    ss << std::boolalpha;
    LIBCWD_USING_OSTREAM_PRELUDE
    ss << __write__("string_literal:") << "literal"
       << __write__("\nstd::string:") << string_value
       << __write__("\nbool:") << bool_value
       << __write__("\nint:") << int_value
       << __write__("\nenum_to_string:") << color_value
       << __write__("\nenum_plain:") << plain_value
       << __write__("\nmutex:") << mutex_value
       << __write__("\noptional_string:") << optional_string_value
       << __write__("\noptional_string_empty:") << optional_string_empty
       << __write__("\nFoo:") << foo
       << __write__("\nintrusive_ptr:") << foo_ptr
       << __write__("\noptional_intrusive_ptr:") << optional_foo
       << __write__("\noptional_intrusive_ptr_empty:") << optional_foo_empty
       << '\n';
  }

  std::cout << "Result: " << ss.str();
  return ss.str();
}

void test_string_literal_renders_raw()
{
  std::stringstream ss;
  {
    ss << "<prefix>";
    LIBCWD_USING_OSTREAM_PRELUDE
    ss << __write__("literal:")
       << "literal";
  }
  expect(ss.str() == "<prefix>literal:\"literal\"", "prefix renders raw, string literal is quoted");
}

void test_std_string_is_quoted()
{
  std::stringstream ss;
  std::string const value = "hello";
  {
    LIBCWD_USING_OSTREAM_PRELUDE
    ss << value;
  }
  expect(ss.str() == "\"hello\"", "std::string renders double-quoted");
}

void test_bool_renders_boolalpha()
{
  // NOTE: boolalpha is not yet handled by the prelude operators. This assertion
  // encodes the desired contract; tighten the project operators to make it pass.
  std::stringstream ss;
  {
    ss << std::boolalpha;
    LIBCWD_USING_OSTREAM_PRELUDE
    ss << true;
  }
  expect(ss.str() == "true", "bool renders through std::boolalpha");
}

void test_int_renders_value()
{
  std::stringstream ss;
  {
    ss << std::boolalpha;
    LIBCWD_USING_OSTREAM_PRELUDE
    ss << 42;
  }
  expect(ss.str() == "42", "int renders its value");
}

void test_enum_with_to_string_renders_name()
{
  std::stringstream ss;
  {
    LIBCWD_USING_OSTREAM_PRELUDE
    ss << test_adl::Color::Green;
  }
  expect(ss.str() == "GREEN", "enum with to_string renders using that to_string");
}

void test_enum_without_to_string_renders()
{
  // NOTE: scoped enums without to_string are not yet printable through the
  // prelude; this is the gap to fill. The expectation is intentionally lenient.
  std::stringstream ss;
  {
    LIBCWD_USING_OSTREAM_PRELUDE
    ss << Plain::Beta;
  }
  expect(ss.str() == "Beta", "enum without to_string renders using enchantum");
}

void test_mutex_renders_marker()
{
  std::stringstream ss;
  std::mutex m;
  {
    LIBCWD_USING_OSTREAM_PRELUDE
    ss << m;
  }
  expect(ss.str() == "$mutex$", "std::mutex renders its marker");
}

void test_optional_string_renders_value_or_marker()
{
  {
    std::stringstream ss;
    std::optional<std::string> const value = std::string("present");
    {
      LIBCWD_USING_OSTREAM_PRELUDE
      ss << value;
    }
    if (ss.str() != "\"present\"")
      std::cout << "ss.str() = \"" << ss.str() << "\"." << std::endl;
    expect(ss.str() == "\"present\"", "engaged optional<string> renders the quoted value");
  }
  {
    std::stringstream ss;
    std::optional<std::string> const empty;
    {
      LIBCWD_USING_OSTREAM_PRELUDE
      ss << empty;
    }
    expect(ss.str() == "$no value$", "disengaged optional renders the marker");
  }
}

void test_optional_intrusive_ptr_renders()
{
  std::stringstream ss;
  boost::intrusive_ptr<ava::session::Foo> const foo(new ava::session::Foo);
  std::optional<boost::intrusive_ptr<ava::session::Foo>> const value(foo);
  {
    LIBCWD_USING_OSTREAM_PRELUDE
    ss << value;
  }
  // The engaged case should dereference through print_pointer and show {Foo}.
  expect(ss.str().find("{Foo}") != std::string::npos, "engaged optional<intrusive_ptr> dereferences Foo");

  std::stringstream ss_empty;
  std::optional<boost::intrusive_ptr<ava::session::Foo>> const empty;
  {
    LIBCWD_USING_OSTREAM_PRELUDE
    ss_empty << empty;
  }
  expect(ss_empty.str() == "$no value$", "disengaged optional<intrusive_ptr> renders the marker");
}

void test_full_render_smoke()
{
  // Exercises every requested type in one stream; mainly a compile-and-run smoke.
  std::string const rendered = render_debug_values();
  expect(!rendered.empty(), "combined render produces output");
}

} // namespace

void run_debug_tests()
{
  test_string_literal_renders_raw();
  test_std_string_is_quoted();
  test_bool_renders_boolalpha();
  test_int_renders_value();
  test_enum_with_to_string_renders_name();
  test_enum_without_to_string_renders();
  test_mutex_renders_marker();
  test_optional_string_renders_value_or_marker();
  test_optional_intrusive_ptr_renders();
  test_full_render_smoke();
}
